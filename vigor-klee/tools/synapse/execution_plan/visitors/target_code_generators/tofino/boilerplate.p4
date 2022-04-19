#include <core.p4>
#include <v1model.p4>

#define BROADCAST_PORT 511
#define DROP_PORT 510
#define CPU_PORT  509

typedef bit<16> mcast_group_t;

/**************** H E A D E R S  ****************/

@controller_header("packet_in")
header packet_in_t {
  bit<32> code_id;
  bit<9> src_device;
  bit<7> pad;
}

@controller_header("packet_out")
header packet_out_t {
  bit<9> src_device;
  bit<9> dst_device;
  bit<6> pad;
}

{{headers_definitions}}

struct headers {
  packet_in_t packet_in;
  packet_out_t packet_out;
  {{headers_declarations}}
}

struct metadata {
  {{metadata_fields}}
}

/****************************************************************
*************************  P A R S E R  *************************
****************************************************************/

parser SyNAPSE_Parser(packet_in packet,
                      out headers hdr,
                      inout metadata meta,
                      inout standard_metadata_t standard_metadata) {
  state start {
    transition select(standard_metadata.ingress_port) {
      CPU_PORT: parse_packet_out;
      default: parse_headers;
    }
  }

  state parse_packet_out {
    packet.extract(hdr.packet_out);
    transition parse_headers;
  }

  {{parse_headers}}
}

/****************************************************************
********** C H E C K S U M    V E R I F I C A T I O N ***********
****************************************************************/

control SyNAPSE_VerifyChecksum(inout headers hdr,
                               inout metadata meta) {
  apply {}
}

/****************************************************************
************** I N G R E S S   P R O C E S S I N G **************
****************************************************************/

control SyNAPSE_Ingress(inout headers hdr,
                        inout metadata meta,
                        inout standard_metadata_t standard_metadata) {
  
  /**************** B O I L E R P L A T E  ****************/

  {{ingress_tag_versions_action}}  

  table tag_control {
    actions = {
      tag_versions;
    }

    size = 1;
  }

  action broadcast() {
    standard_metadata.mcast_grp = (mcast_group_t) 1;
  }
  
  action drop() {
    standard_metadata.egress_spec = DROP_PORT;
  }

  action forward(bit<9> port) {
    if (port == BROADCAST_PORT) {
      broadcast();
    } else {
      standard_metadata.egress_spec = port;
    }
  }

  action send_to_controller(bit<32> code_id) {
    standard_metadata.egress_spec = CPU_PORT;

    hdr.packet_in.setValid();
    hdr.packet_in.code_id = code_id;
    hdr.packet_in.src_device = standard_metadata.ingress_port;
  }

  {{ingress_globals}}

  apply {
    tag_control.apply();
    
    if (standard_metadata.ingress_port == CPU_PORT) {
      forward(hdr.packet_out.dst_device);
      return;
    }

    {{ingress_apply_content}}
  }
}

/****************************************************************
*************** E G R E S S   P R O C E S S I N G ***************
****************************************************************/

control SyNAPSE_Egress(inout headers hdr,
                       inout metadata meta,
                       inout standard_metadata_t standard_metadata) {
  action drop() {
    standard_metadata.egress_spec = DROP_PORT;
  }

  apply {
    if (standard_metadata.ingress_port == CPU_PORT
      && hdr.packet_out.src_device == standard_metadata.egress_port) {
      drop();
    }
  }
}

/****************************************************************
**********  C H E C K S U M    C O M P U T A T I O N   **********
****************************************************************/

control SyNAPSE_ComputeChecksum(inout headers hdr,
                                inout metadata meta) {
  apply {}
}

/****************************************************************
***********************  D E P A R S E R  ***********************
****************************************************************/

control SyNAPSE_Deparser(packet_out packet,
                         in headers hdr) {
  apply {
    packet.emit(hdr.packet_in);
    {{deparser_apply}}
  }
}

/****************************************************************
************************** S W I T C H **************************
****************************************************************/

V1Switch(SyNAPSE_Parser(),
         SyNAPSE_VerifyChecksum(),
         SyNAPSE_Ingress(),
         SyNAPSE_Egress(),
         SyNAPSE_ComputeChecksum(),
         SyNAPSE_Deparser()
) main;
