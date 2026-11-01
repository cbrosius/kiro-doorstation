#include "unity.h"
#include "auth_manager.h"
#include "dtmf_decoder.h"
#include "gpio_handler.h"
#include "rtp_handler.h"
#include "sip_auth.h"
#include "sip_msg.h"
#include "sip_utils.h"
#include <string.h>

TEST_CASE("sip_utils calculate_md5_hex computes RFC 1321 test vectors", "[sip_utils]")
{
    char md5_out[33] = {0};
    calculate_md5_hex("", md5_out);
    TEST_ASSERT_EQUAL_STRING("d41d8cd98f00b204e9800998ecf8427e", md5_out);

    calculate_md5_hex("abc", md5_out);
    TEST_ASSERT_EQUAL_STRING("900150983cd24fb0d6963f7d28e17f72", md5_out);
}

TEST_CASE("sip_utils extract_quoted_value and extract_received_ip parse headers", "[sip_utils]")
{
    const char *hdr = "Digest realm=\"asterisk\", nonce=\"12345678\", qop=\"auth\"";
    char val[64] = {0};
    TEST_ASSERT_TRUE(extract_quoted_value(hdr, "realm=", val, sizeof(val)));
    TEST_ASSERT_EQUAL_STRING("asterisk", val);

    TEST_ASSERT_TRUE(extract_quoted_value(hdr, "nonce=", val, sizeof(val)));
    TEST_ASSERT_EQUAL_STRING("12345678", val);

    const char *via = "Via: SIP/2.0/UDP 192.168.1.50:5060;branch=z9hG4bK123;received=203.0.113.10;rport=5060\r\n";
    char ip[16] = {0};
    TEST_ASSERT_TRUE(extract_received_ip(via, ip, sizeof(ip)));
    TEST_ASSERT_EQUAL_STRING("203.0.113.10", ip);
}

TEST_CASE("sip_auth parse_www_authenticate and calculate_digest_response", "[sip_auth]")
{
    const char *msg =
        "SIP/2.0 401 Unauthorized\r\n"
        "WWW-Authenticate: Digest algorithm=MD5, realm=\"fritz.box\", nonce=\"ABCDEF123456\", qop=\"auth\"\r\n\r\n";

    sip_auth_challenge_t challenge = parse_www_authenticate(msg);
    TEST_ASSERT_TRUE(challenge.valid);
    TEST_ASSERT_EQUAL_STRING("fritz.box", challenge.realm);
    TEST_ASSERT_EQUAL_STRING("ABCDEF123456", challenge.nonce);
    TEST_ASSERT_EQUAL_STRING("auth", challenge.qop);

    char response[33] = {0};
    calculate_digest_response("doorbell", "secret123", challenge.realm,
                              challenge.nonce, "REGISTER", "sip:fritz.box",
                              challenge.qop, "00000001", "deadbeef", response);
    TEST_ASSERT_EQUAL_UINT32(32, strlen(response));
}

TEST_CASE("sip_msg extract_request_headers parses INVITE headers", "[sip_msg]")
{
    const char *invite =
        "INVITE sip:doorbell@192.168.1.100:5060 SIP/2.0\r\n"
        "Via: SIP/2.0/UDP 192.168.1.1:5060;branch=z9hG4bK999\r\n"
        "From: <sip:620@fritz.box>;tag=fromtag123\r\n"
        "To: <sip:doorbell@fritz.box>\r\n"
        "Call-ID: call-98765@192.168.1.1\r\n"
        "CSeq: 42 INVITE\r\n"
        "Content-Length: 0\r\n\r\n";

    sip_request_headers_t hdrs = extract_request_headers(invite);
    TEST_ASSERT_TRUE(hdrs.valid);
    TEST_ASSERT_EQUAL_STRING("call-98765@192.168.1.1", hdrs.call_id);
    TEST_ASSERT_EQUAL_INT(42, hdrs.cseq_num);
    TEST_ASSERT_EQUAL_STRING("INVITE", hdrs.cseq_method);
}

TEST_CASE("gpio_handler unified door relay activation and deactivation", "[gpio_handler]")
{
    door_relay_deactivate();
    TEST_ASSERT_FALSE(door_relay_is_active());
    TEST_ASSERT_EQUAL_UINT32(0, door_relay_get_remaining_ms());

    door_relay_activate_for_ms(2000);
    TEST_ASSERT_TRUE(door_relay_is_active());
    TEST_ASSERT_GREATER_THAN_UINT32(0, door_relay_get_remaining_ms());

    door_relay_deactivate();
    TEST_ASSERT_FALSE(door_relay_is_active());
    TEST_ASSERT_EQUAL_UINT32(0, door_relay_get_remaining_ms());
}

TEST_CASE("auth_manager password policy and rate limiting", "[auth_manager]")
{
    TEST_ASSERT_FALSE(auth_validate_password("short", NULL, 0));
    TEST_ASSERT_TRUE(auth_validate_password("ValidPass123!", NULL, 0));

    const char *test_ip = "192.0.2.99";
    auth_clear_failed_attempts(test_ip);
    TEST_ASSERT_FALSE(auth_is_ip_blocked(test_ip));

    for (int i = 0; i < 5; i++) {
        auth_record_failed_attempt(test_ip);
    }
    TEST_ASSERT_TRUE(auth_is_ip_blocked(test_ip));

    auth_clear_all_failed_attempts();
    TEST_ASSERT_FALSE(auth_is_ip_blocked(test_ip));
}

TEST_CASE("rtp_handler inactive session safety", "[rtp_handler]")
{
    rtp_init();
    TEST_ASSERT_FALSE(rtp_is_active());
    TEST_ASSERT_EQUAL_INT(-1, rtp_send_dtmf('1'));
}

