#!/router/bin/python

from trex_stl_exceptions import *
from trex_stl_types import verify_exclusive_arg, validate_type
from trex_stl_packet_builder_interface import CTrexPktBuilderInterface
from trex_stl_packet_builder_scapy import CScapyTRexPktBuilder, Ether, IP, UDP, TCP, RawPcapReader
from collections import OrderedDict, namedtuple

from dpkt import pcap
import random
import yaml
import base64
import string
import traceback
from types import NoneType
import copy


# base class for TX mode
class STLTXMode(object):
    def __init__ (self, pps = None, bps_L1 = None, bps_L2 = None, percentage = None):
        args = [pps, bps_L1, bps_L2, percentage]

        # default
        if all([x is None for x in args]):
            pps = 1.0
        else:
            verify_exclusive_arg(args)

        self.fields = {'rate': {}}

        if pps is not None:
            validate_type('pps', pps, [float, int])

            self.fields['rate']['type']  = 'pps'
            self.fields['rate']['value'] = pps

        elif bps_L1 is not None:
            validate_type('bps_L1', bps_L1, [float, int])

            self.fields['rate']['type']  = 'bps_L1'
            self.fields['rate']['value'] = bps_L1

        elif bps_L2 is not None:
            validate_type('bps_L2', bps_L2, [float, int])

            self.fields['rate']['type']  = 'bps_L2'
            self.fields['rate']['value'] = bps_L2

        elif percentage is not None:
            validate_type('percentage', percentage, [float, int])
            if not (percentage > 0 and percentage <= 100):
                raise STLArgumentError('percentage', percentage)

            self.fields['rate']['type']  = 'percentage'
            self.fields['rate']['value'] = percentage

        

    def to_json (self):
        return self.fields


# continuous mode
class STLTXCont(STLTXMode):

    def __init__ (self, **kwargs):

        super(STLTXCont, self).__init__(**kwargs)

        self.fields['type'] = 'continuous'

    def __str__ (self):
        return "Continuous"

# single burst mode
class STLTXSingleBurst(STLTXMode):

    def __init__ (self, total_pkts = 1, **kwargs):

        if not isinstance(total_pkts, int):
            raise STLArgumentError('total_pkts', total_pkts)

        super(STLTXSingleBurst, self).__init__(**kwargs)

        self.fields['type'] = 'single_burst'
        self.fields['total_pkts'] = total_pkts

    def __str__ (self):
        return "Single Burst"

# multi burst mode
class STLTXMultiBurst(STLTXMode):

    def __init__ (self,
                  pkts_per_burst = 1,
                  ibg = 0.0,   # usec not SEC
                  count = 1,
                  **kwargs):

        if not isinstance(pkts_per_burst, int):
            raise STLArgumentError('pkts_per_burst', pkts_per_burst)

        if not isinstance(ibg, (int, float)):
            raise STLArgumentError('ibg', ibg)

        if not isinstance(count, int):
            raise STLArgumentError('count', count)

        super(STLTXMultiBurst, self).__init__(**kwargs)

        self.fields['type'] = 'multi_burst'
        self.fields['pkts_per_burst'] = pkts_per_burst
        self.fields['ibg'] = ibg
        self.fields['count'] = count

    def __str__ (self):
        return "Multi Burst"

STLStreamDstMAC_CFG_FILE=0
STLStreamDstMAC_PKT     =1
STLStreamDstMAC_ARP     =2

# RX stats class
class STLRxStats(object):
    def __init__ (self, user_id):
        self.fields = {}

        self.fields['enabled']         = True
        self.fields['stream_id']       = user_id
        self.fields['seq_enabled']     = False
        self.fields['latency_enabled'] = False


    def to_json (self):
        return dict(self.fields)

    @staticmethod
    def defaults ():
        return {'enabled' : False}

class STLStream(object):

    def __init__ (self,
                  name = None,
                  packet = None,
                  mode = STLTXCont(pps = 1),
                  enabled = True,
                  self_start = True,
                  isg = 0.0,
                  rx_stats = None,
                  next = None,
                  stream_id = None,
                  action_count = 0,
                  random_seed =0,
                  mac_src_override_by_pkt=None,
                  mac_dst_override_mode=None    #see  STLStreamDstMAC_xx
                  ):

        # type checking
        validate_type('mode', mode, STLTXMode)
        validate_type('packet', packet, (NoneType, CTrexPktBuilderInterface))
        validate_type('enabled', enabled, bool)
        validate_type('self_start', self_start, bool)
        validate_type('isg', isg, (int, float))
        validate_type('stream_id', stream_id, (NoneType, int))
        validate_type('random_seed',random_seed,int);

        if (type(mode) == STLTXCont) and (next != None):
            raise STLError("continuous stream cannot have a next stream ID")

        # tag for the stream and next - can be anything
        self.name = name
        self.next = next

        self.id = stream_id


        self.fields = {}

        int_mac_src_override_by_pkt = 0;
        int_mac_dst_override_mode   = 0;


        if mac_src_override_by_pkt == None:
            int_mac_src_override_by_pkt=0
            if packet :
                if packet.is_def_src_mac ()==False:
                    int_mac_src_override_by_pkt=1

        else:
            int_mac_src_override_by_pkt = int(mac_src_override_by_pkt);

        if mac_dst_override_mode == None:
            int_mac_dst_override_mode   = 0;
            if packet :
                if packet.is_def_dst_mac ()==False:
                    int_mac_dst_override_mode=STLStreamDstMAC_PKT
        else:
            int_mac_dst_override_mode = int(mac_dst_override_mode);


        self.fields['flags'] = (int_mac_src_override_by_pkt&1) +  ((int_mac_dst_override_mode&3)<<1)

        self.fields['action_count'] = action_count

        # basic fields
        self.fields['enabled'] = enabled
        self.fields['self_start'] = self_start
        self.fields['isg'] = isg

        if random_seed !=0 :
            self.fields['random_seed'] = random_seed # optional

        # mode
        self.fields['mode'] = mode.to_json()
        self.mode_desc      = str(mode)


        # packet
        self.fields['packet'] = {}
        self.fields['vm'] = {}

        if not packet:
            packet = CScapyTRexPktBuilder(pkt = Ether()/IP())

        # packet builder
        packet.compile()

        # packet and VM
        self.fields['packet'] = packet.dump_pkt()
        self.fields['vm']     = packet.get_vm_data()

        self.pkt = base64.b64decode(self.fields['packet']['binary'])

        # this is heavy, calculate lazy
        self.packet_desc = None

        if not rx_stats:
            self.fields['rx_stats'] = STLRxStats.defaults()
        else:
            self.fields['rx_stats'] = rx_stats.to_json()


    def __str__ (self):
        s =  "Stream Name: {0}\n".format(self.name)
        s += "Stream Next: {0}\n".format(self.next)
        s += "Stream JSON:\n{0}\n".format(json.dumps(self.fields, indent = 4, separators=(',', ': '), sort_keys = True))
        return s

    def to_json (self):
        return dict(self.fields)

    def get_id (self):
        return self.id


    def get_name (self):
        return self.name

    def get_next (self):
        return self.next


    def get_pkt (self):
        return self.pkt

    def get_pkt_len (self, count_crc = True):
       pkt_len = len(self.get_pkt())
       if count_crc:
           pkt_len += 4

       return pkt_len


    def get_pkt_type (self):
        if self.packet_desc == None:
            self.packet_desc = CScapyTRexPktBuilder.pkt_layers_desc_from_buffer(self.get_pkt())

        return self.packet_desc

    def get_mode (self):
        return self.mode_desc

    @staticmethod
    def get_rate_from_field (rate_json):
        t = rate_json['type']
        v = rate_json['value']

        if t == "pps":
            return format_num(v, suffix = "pps")
        elif t == "bps_L1":
            return format_num(v, suffix = "bps (L1)")
        elif t == "bps_L2":
            return format_num(v, suffix = "bps (L2)")
        elif t == "percentage":
            return format_num(v, suffix = "%")

    def get_rate (self):
        return self.get_rate_from_field(self.fields['mode']['rate'])


    def to_yaml (self):
        y = {}

        if self.name:
            y['name'] = self.name

        if self.next:
            y['next'] = self.next

        y['stream'] = copy.deepcopy(self.fields)
        
        # some shortcuts for YAML
        rate_type  = self.fields['mode']['rate']['type']
        rate_value = self.fields['mode']['rate']['value']

        y['stream']['mode'][rate_type] = rate_value
        del y['stream']['mode']['rate']

        return y
  
    def dump_to_yaml (self, yaml_file = None):
        yaml_dump = yaml.dump([self.to_yaml()], default_flow_style = False)

        # write to file if provided
        if yaml_file:
            with open(yaml_file, 'w') as f:
                f.write(yaml_dump)

        return yaml_dump

class YAMLLoader(object):

    def __init__ (self, yaml_file):
        self.yaml_path = os.path.dirname(yaml_file)
        self.yaml_file = yaml_file


    def __parse_packet (self, packet_dict):

        packet_type = set(packet_dict).intersection(['binary', 'pcap'])
        if len(packet_type) != 1:
            raise STLError("packet section must contain either 'binary' or 'pcap'")

        if 'binary' in packet_type:
            try:
                pkt_str = base64.b64decode(packet_dict['binary'])
            except TypeError:
                raise STLError("'binary' field is not a valid packet format")

            builder = CScapyTRexPktBuilder(pkt_buffer = pkt_str)

        elif 'pcap' in packet_type:
            pcap = os.path.join(self.yaml_path, packet_dict['pcap'])

            if not os.path.exists(pcap):
                raise STLError("'pcap' - cannot find '{0}'".format(pcap))

            builder = CScapyTRexPktBuilder(pkt = pcap)

        return builder


    def __parse_mode (self, mode_obj):
        if not mode_obj:
            return None

        rate_parser = set(mode_obj).intersection(['pps', 'bps_L1', 'bps_L2', 'percentage'])
        if len(rate_parser) != 1:
            raise STLError("'rate' must contain exactly one from 'pps', 'bps_L1', 'bps_L2', 'percentage'")

        rate_type  = rate_parser.pop()
        rate = {rate_type : mode_obj[rate_type]}

        mode_type = mode_obj.get('type')

        if mode_type == 'continuous':
            mode = STLTXCont(**rate)

        elif mode_type == 'single_burst':
            defaults = STLTXSingleBurst()
            mode = STLTXSingleBurst(total_pkts  = mode_obj.get('total_pkts', defaults.fields['total_pkts']),
                                    **rate)

        elif mode_type == 'multi_burst':
            defaults = STLTXMultiBurst()
            mode = STLTXMultiBurst(pkts_per_burst = mode_obj.get('pkts_per_burst', defaults.fields['pkts_per_burst']),
                                   ibg            = mode_obj.get('ibg', defaults.fields['ibg']),
                                   count          = mode_obj.get('count', defaults.fields['count']),
                                   **rate)

        else:
            raise STLError("mode type can be 'continuous', 'single_burst' or 'multi_burst")


        return mode



    def __parse_rx_stats (self, rx_stats_obj):

        # no such object
        if not rx_stats_obj or rx_stats_obj.get('enabled') == False:
            return None

        user_id = rx_stats_obj.get('stream_id') 
        if user_id == None:
            raise STLError("enabled RX stats section must contain 'stream_id' field")

        return STLRxStats(user_id = user_id)


    def __parse_stream (self, yaml_object):
        s_obj = yaml_object['stream']

        # parse packet
        packet = s_obj.get('packet')
        if not packet:
            raise STLError("YAML file must contain 'packet' field")

        builder = self.__parse_packet(packet)


        # mode
        mode = self.__parse_mode(s_obj.get('mode'))

        # rx stats
        rx_stats = self.__parse_rx_stats(s_obj.get('rx_stats'))
        

        defaults = STLStream()
        # create the stream
        stream = STLStream(name       = yaml_object.get('name'),
                           packet     = builder,
                           mode       = mode,
                           rx_stats   = rx_stats,
                           enabled    = s_obj.get('enabled', defaults.fields['enabled']),
                           self_start = s_obj.get('self_start', defaults.fields['self_start']),
                           isg        = s_obj.get('isg', defaults.fields['isg']),
                           next       = yaml_object.get('next'),
                           action_count = s_obj.get('action_count', defaults.fields['action_count']),
                           mac_src_override_by_pkt = s_obj.get('mac_src_override_by_pkt', 0),
                           mac_dst_override_mode = s_obj.get('mac_src_override_by_pkt', 0) 
                           )

        # hack the VM fields for now
        if 'vm' in s_obj:
            stream.fields['vm'].update(s_obj['vm'])

        return stream


    def parse (self):
        with open(self.yaml_file, 'r') as f:
            # read YAML and pass it down to stream object
            yaml_str = f.read()

            try:
                objects = yaml.load(yaml_str)
            except yaml.parser.ParserError as e:
                raise STLError(str(e))

            streams = [self.__parse_stream(object) for object in objects]
            
            return streams


# profile class
class STLProfile(object):
    def __init__ (self, streams = None):
        if streams == None:
            streams = []

        if not type(streams) == list:
            streams = [streams]

        if not all([isinstance(stream, STLStream) for stream in streams]):
            raise STLArgumentError('streams', streams, valid_values = STLStream)

        self.streams = streams


    def get_streams (self):
        return self.streams

    def __str__ (self):
        return '\n'.join([str(stream) for stream in self.streams])


    @staticmethod
    def load_yaml (yaml_file):
        # check filename
        if not os.path.isfile(yaml_file):
            raise STLError("file '{0}' does not exists".format(yaml_file))

        yaml_loader = YAMLLoader(yaml_file)
        streams = yaml_loader.parse()

        return STLProfile(streams)


    @staticmethod
    def load_py (python_file):
        # check filename
        if not os.path.isfile(python_file):
            raise STLError("file '{0}' does not exists".format(python_file))

        basedir = os.path.dirname(python_file)
        sys.path.append(basedir)

        try:
            file    = os.path.basename(python_file).split('.')[0]
            module = __import__(file, globals(), locals(), [], -1)
            reload(module) # reload the update 

            streams = module.register().get_streams()

            return STLProfile(streams)

        except Exception as e:
            a, b, tb = sys.exc_info()
            x =''.join(traceback.format_list(traceback.extract_tb(tb)[1:])) + a.__name__ + ": " + str(b) + "\n"

            summary = "\nPython Traceback follows:\n\n" + x
            raise STLError(summary)


        finally:
            sys.path.remove(basedir)

    
    # loop_count = 0 means loop forever
    @staticmethod
    def load_pcap (pcap_file, ipg_usec = None, speedup = 1.0, loop_count = 1, vm = None):
        # check filename
        if not os.path.isfile(pcap_file):
            raise STLError("file '{0}' does not exists".format(pcap_file))

        streams = []
        last_ts_usec = 0

        pkts = RawPcapReader(pcap_file).read_all()
        
        for i, (cap, meta) in enumerate(pkts, start = 1):
            # IPG - if not provided, take from cap
            if ipg_usec == None:
                ts_usec = (meta[0] * 1e6 + meta[1]) / float(speedup)
            else:
                ts_usec = (ipg_usec * i) / float(speedup)

            # handle last packet
            if i == len(pkts):
                next = 1
                action_count = loop_count
            else:
                next = i + 1
                action_count = 0

            
            streams.append(STLStream(name = i,
                                     packet = CScapyTRexPktBuilder(pkt_buffer = cap, vm = vm),
                                     mode = STLTXSingleBurst(total_pkts = 1, percentage = 100),
                                     self_start = True if (i == 1) else False,
                                     isg = (ts_usec - last_ts_usec),  # seconds to usec
                                     action_count = action_count,
                                     next = next))
        
            last_ts_usec = ts_usec


        return STLProfile(streams)

      

    @staticmethod
    def load (filename):
        x = os.path.basename(filename).split('.')
        suffix = x[1] if (len(x) == 2) else None

        if suffix == 'py':
            profile = STLProfile.load_py(filename)

        elif suffix == 'yaml':
            profile = STLProfile.load_yaml(filename)

        elif suffix in ['cap', 'pcap']:
            profile = STLProfile.load_pcap(filename, speedup = 1, ipg_usec = 1e6)

        else:
            raise STLError("unknown profile file type: '{0}'".format(suffix))

        return profile


    def dump_to_yaml (self, yaml_file = None):
        yaml_list = [stream.to_yaml() for stream in self.streams]
        yaml_str = yaml.dump(yaml_list, default_flow_style = False)

        # write to file if provided
        if yaml_file:
            with open(yaml_file, 'w') as f:
                f.write(yaml_str)

        return yaml_str


    def __len__ (self):
        return len(self.streams)

