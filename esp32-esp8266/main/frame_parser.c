#include "frame_parser.h"

#include <string.h>

#include "ids_types.h"

// Cabeçalho de gerenciamento: FC(2) Duration(2) addr1(6) addr2(6) addr3(6) SeqCtl(2) = 24 bytes.
#define MGMT_HDR_LEN 24
#define ADDR3_OFFSET 16

bool frame_parse_mgmt(const uint8_t *frame, uint16_t len, uint8_t *subtype, uint8_t bssid[6])
{
    if (frame == NULL || len < MGMT_HDR_LEN) {
        return false;
    }
    uint8_t fc0 = frame[0];  // bits 0-1 versão, 2-3 tipo, 4-7 subtipo
    if ((fc0 & 0x03) != 0 || (fc0 & 0x0C) != 0) {
        return false;  // versão desconhecida ou não é gerenciamento
    }
    uint8_t st = fc0 >> 4;
    if (st != SUB_PROBE_REQ && st != SUB_BEACON && st != SUB_DISASSOC && st != SUB_DEAUTH) {
        return false;
    }
    *subtype = st;
    // Em frames de gerenciamento addr3 é o BSSID; em probe request costuma ser broadcast (ff:ff:...).
    memcpy(bssid, frame + ADDR3_OFFSET, 6);
    return true;
}
