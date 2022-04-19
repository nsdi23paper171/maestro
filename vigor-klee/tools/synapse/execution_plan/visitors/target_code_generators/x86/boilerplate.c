#define DATA_BUFFER_SZ 512

unsigned char data_buffer[DATA_BUFFER_SZ];
unsigned data_buffer_offset;

{{global_state}}

{{runtime_configure}}

bool nf_init() {
  {{nf_init}}
}

int nf_process(uint16_t device, uint8_t **p, uint16_t pkt_len, int64_t now, struct rte_mbuf *mbuf) {
  data_buffer_offset = 0;
  {{nf_process}}
}
