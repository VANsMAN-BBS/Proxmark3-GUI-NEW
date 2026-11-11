import pytest

from emrtd_pm3 import tlv


def test_short_length_roundtrip():
    encoded = tlv.encode(0x5A, b"abc")
    tag, value, pos = tlv.parse(encoded)
    assert tag == 0x5A
    assert value == b"abc"
    assert pos == len(encoded)


def test_multi_byte_tag_roundtrip():
    encoded = tlv.encode(0x7F61, b"payload")
    tag, value, pos = tlv.parse(encoded)
    assert tag == 0x7F61
    assert value == b"payload"
    assert pos == len(encoded)


@pytest.mark.parametrize("length", [0, 1, 127, 128, 255, 256, 1024, 65535])
def test_length_forms(length):
    value = b"\xaa" * length
    tag, decoded, pos = tlv.parse(tlv.encode(0x60, value))
    assert tag == 0x60
    assert decoded == value
    assert pos == len(tlv.encode(0x60, value))


def test_iter_and_nested():
    data = tlv.encode(0x60, tlv.encode(0x5F01, b"\x01\x07") + tlv.encode(0x5C, b"\x61\x75"))
    assert list(tlv.iter_tlv(data))[0][0] == 0x60
    assert tlv.find_first(data, 0x5F01) == b"\x01\x07"
    assert tlv.find_first(data, 0x5C) == b"\x61\x75"


def test_truncated_input_raises():
    with pytest.raises(ValueError):
        tlv.parse(b"\x60")
    with pytest.raises(ValueError):
        tlv.parse(b"\x60\x05abc")
