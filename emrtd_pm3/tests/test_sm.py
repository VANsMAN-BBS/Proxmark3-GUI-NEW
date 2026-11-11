import pytest

from emrtd_pm3 import sm

KENC = bytes.fromhex("AB94FDECF2674FDFB9B391F85D7F76F2")
KMAC = bytes.fromhex("7962D9ECE03D1ACD4C76089DCE131543")
SSC0 = int.from_bytes(bytes.fromhex("0102030405060708"), "big")


def pair() -> tuple[sm.SecureMessaging, sm.SecureMessaging]:
    return (
        sm.SecureMessaging(KENC, KMAC, SSC0),
        sm.SecureMessaging(KENC, KMAC, SSC0),
    )


def test_read_binary_roundtrip():
    client, chip = pair()
    command = client.encode(0xB0, 0x00, 0x10, le=32)

    assert command[0] == 0x0C and command[1] == 0xB0
    body = command[5:5 + command[4]]
    data, le = chip.decode_command(0xB0, 0x00, 0x10, body)
    assert data == b""
    assert le == 32

    response = chip.encode_response(b"hello world")
    status, payload = client.decode(response)
    assert status == 0x9000
    assert payload == b"hello world"


def test_select_ef_has_no_le():
    client, chip = pair()
    command = client.encode(0xA4, 0x02, 0x0C, b"\x01\x1e")
    data, le = chip.decode_command(0xA4, 0x02, 0x0C, command[5:5 + command[4]])
    assert data == b"\x01\x1e"
    assert le is None


def test_sequence_of_commands_keeps_ssc_aligned():
    client, chip = pair()
    for index in range(5):
        payload = bytes([index]) * (index + 1)
        command = client.encode(0xB0, 0x00, 0x00, le=16)
        data, le = chip.decode_command(0xB0, 0x00, 0x00, command[5:5 + command[4]])
        assert le == 16

        response = chip.encode_response(payload)
        status, out = client.decode(response)
        assert status == 0x9000
        assert out == payload


def test_tampered_response_mac_is_rejected():
    client, chip = pair()
    client.encode(0xB0, 0x00, 0x00, le=16)

    response = bytearray(chip.encode_response(b"data"))
    response[0] ^= 0xFF  # break DO87
    with pytest.raises(sm.SecureMessagingError):
        client.decode(bytes(response))


def test_tampered_command_mac_is_rejected():
    client, chip = pair()
    command = client.encode(0xB0, 0x00, 0x00, le=16)
    body = bytearray(command[5:5 + command[4]])
    body[-1] ^= 0x01
    with pytest.raises(sm.SecureMessagingError):
        chip.decode_command(0xB0, 0x00, 0x00, bytes(body))
