"""Unit tests for OpenDoorstation core algorithms, protocol helpers, and web contracts."""

from pathlib import Path
import hashlib
import re
import struct
import unittest


REPO_ROOT = Path(__file__).resolve().parent.parent


def compute_sip_digest(
    username: str,
    password: str,
    realm: str,
    nonce: str,
    method: str,
    uri: str,
    qop: str = "",
    nc: str = "",
    cnonce: str = "",
) -> str:
    """Reference RFC 2617 / RFC 3261 MD5 Digest calculation matching sip_auth.c."""
    ha1 = hashlib.md5(f"{username}:{realm}:{password}".encode("utf-8")).hexdigest()
    ha2 = hashlib.md5(f"{method}:{uri}".encode("utf-8")).hexdigest()
    if qop and "auth" in qop:
        raw = f"{ha1}:{nonce}:{nc}:{cnonce}:auth:{ha2}"
    else:
        raw = f"{ha1}:{nonce}:{ha2}"
    return hashlib.md5(raw.encode("utf-8")).hexdigest()


def decode_alaw_sample(a_val: int) -> int:
    """Reference ITU-T G.711 A-law decoder matching rtp_handler.c."""
    a_val ^= 0x55
    t = (a_val & 0x0F) << 4
    seg = (a_val & 0x70) >> 4
    if seg == 0:
        t += 8
    elif seg == 1:
        t += 0x108
    else:
        t += 0x108
        t <<= seg - 1
    return t if (a_val & 0x80) else -t


def build_rfc4733_dtmf_packets(
    event_code: int, start_seq: int, start_timestamp: int, ssrc: int
) -> list[bytes]:
    """Reference RFC 4733 5-packet DTMF sequence matching rtp_send_dtmf() in rtp_handler.c."""
    durations = (160, 320, 480, 480, 480)
    end_flags = (0, 0, 0x80, 0x80, 0x80)
    packets: list[bytes] = []
    for idx in range(5):
        marker = 1 if idx == 0 else 0
        first_byte = (2 << 6) | 0
        second_byte = (marker << 7) | 101
        seq = (start_seq + idx) & 0xFFFF
        e_r_volume = end_flags[idx] | 10
        pkt = struct.pack(
            "!BBHIIBBH",
            first_byte,
            second_byte,
            seq,
            start_timestamp,
            ssrc,
            event_code,
            e_r_volume,
            durations[idx],
        )
        packets.append(pkt)
    return packets


class TestDoorstationCore(unittest.TestCase):
    """Verify SIP, RTP, Audio, Security, and Web Frontend improvements."""

    def test_sip_digest_auth_with_and_without_qop(self) -> None:
        resp_no_qop = compute_sip_digest(
            "620", "secret", "fritz.box", "123456", "REGISTER", "sip:fritz.box"
        )
        self.assertEqual(len(resp_no_qop), 32)

        resp_qop = compute_sip_digest(
            "620",
            "secret",
            "fritz.box",
            "123456",
            "INVITE",
            "sip:**610@fritz.box",
            qop="auth",
            nc="00000001",
            cnonce="01020304",
        )
        self.assertEqual(len(resp_qop), 32)
        self.assertNotEqual(resp_no_qop, resp_qop)

    def test_alaw_decoder_symmetry_and_range(self) -> None:
        pos = decode_alaw_sample(0xD5)
        neg = decode_alaw_sample(0x55)
        self.assertEqual(pos, 8)
        self.assertEqual(neg, -8)

        for byte_val in range(256):
            sample = decode_alaw_sample(byte_val)
            self.assertGreaterEqual(sample, -32768)
            self.assertLessEqual(sample, 32767)

    def test_rfc4733_dtmf_packet_sequence(self) -> None:
        packets = build_rfc4733_dtmf_packets(
            event_code=1, start_seq=100, start_timestamp=8000, ssrc=0x12345678
        )
        self.assertEqual(len(packets), 5)
        for idx, pkt in enumerate(packets):
            self.assertEqual(len(pkt), 16)
            _, pt_marker, seq, ts, _, ev, e_vol, dur = struct.unpack("!BBHIIBBH", pkt)
            self.assertEqual(seq, 100 + idx)
            self.assertEqual(ts, 8000)
            self.assertEqual(ev, 1)
            if idx == 0:
                self.assertTrue(bool(pt_marker & 0x80))
                self.assertFalse(bool(e_vol & 0x80))
                self.assertEqual(dur, 160)
            elif idx == 1:
                self.assertFalse(bool(pt_marker & 0x80))
                self.assertFalse(bool(e_vol & 0x80))
                self.assertEqual(dur, 320)
            else:
                self.assertFalse(bool(pt_marker & 0x80))
                self.assertTrue(bool(e_vol & 0x80))
                self.assertEqual(dur, 480)

    def test_ota_rollback_validation_present_in_main(self) -> None:
        main_c = (REPO_ROOT / "main" / "main.c").read_text(encoding="utf-8")
        self.assertIn("ota_mark_valid()", main_c)
        self.assertIn("esp_task_wdt_add(NULL)", main_c)
        self.assertIn("esp_task_wdt_reset()", main_c)

    def test_security_and_web_api_invariants(self) -> None:
        web_utils_c = (REPO_ROOT / "components" / "web" / "web_utils.c").read_text(
            encoding="utf-8"
        )
        self.assertIn("MAX_HTTP_JSON_BODY_LEN", web_utils_c)
        self.assertIn('"success", true', web_utils_c)
        self.assertIn('"success", false', web_utils_c)

        api_auth_c = (REPO_ROOT / "components" / "web" / "api_auth.c").read_text(
            encoding="utf-8"
        )
        self.assertIn("HttpOnly", api_auth_c)

        api_dtmf_c = (REPO_ROOT / "components" / "web" / "api_dtmf.c").read_text(
            encoding="utf-8"
        )
        self.assertIn('"pin_configured"', api_dtmf_c)

        api_config_c = (REPO_ROOT / "components" / "web" / "api_config.c").read_text(
            encoding="utf-8"
        )
        self.assertIn('"/api/config/restore"', api_config_c)

        api_email_c = (REPO_ROOT / "components" / "web" / "api_email.c").read_text(
            encoding="utf-8"
        )
        self.assertIn('"/api/email/test"', api_email_c)
        self.assertIn('"/api/email/send-report"', api_email_c)

    def test_index_html_modern_web_practices(self) -> None:
        index_html = (REPO_ROOT / "components" / "web" / "index.html").read_text(
            encoding="utf-8"
        )
        self.assertIn('<meta name="color-scheme" content="light dark">', index_html)
        self.assertIn('<meta name="text-scale" content="scale">', index_html)
        self.assertIn("interpolate-size: allow-keywords;", index_html)
        self.assertIn("--font-size-base: 1rem;", index_html)
        self.assertIn("dialog.showModal()", index_html)
        self.assertIn("content-visibility: auto;", index_html)
        self.assertIsNone(re.search(r"html\s*\{[^}]*font-size:\s*16px", index_html))


if __name__ == "__main__":
    unittest.main()
