from emrtd_pm3 import crypto


def test_padding_method_2_vectors():
    assert crypto.pad(bytes.fromhex("1234567880")) == bytes.fromhex("1234567880800000")
    assert crypto.pad(bytes.fromhex("1234")) == bytes.fromhex("1234800000000000")
    assert crypto.pad(b"") == bytes.fromhex("8000000000000000")
    assert crypto.pad(bytes.fromhex("1234567890ABCDEF")) == bytes.fromhex(
        "1234567890ABCDEF8000000000000000"
    )


def test_unpadding_roundtrip():
    for raw in (b"", b"1", b"12345678", bytes(range(16))):
        assert crypto.unpad(crypto.pad(raw)) == raw


def test_check_digits_icao_specimen():
    assert crypto.check_digit("L898902C3") == "6"
    assert crypto.check_digit("740812") == "2"
    assert crypto.check_digit("120415") == "9"
    assert crypto.check_digit("D23145890") == "7"


def test_mrz_information():
    assert crypto.mrz_information("L898902C3", "740812", "120415") == (
        "L898902C3674081221204159"
    )


def test_kdf_vectors():
    seed = bytes.fromhex("239AB9CB282DAF66231DC5A4DF6BFBAE")
    assert crypto.kdf(seed, 1).hex().upper() == "AB94FDECF2674FDFB9B391F85D7F76F2"
    assert crypto.kdf(seed, 2).hex().upper() == "7962D9ECE03D1ACD4C76089DCE131543"


def test_retail_mac_vectors():
    vectors = [
        ("e3857ca24946251c151c540e13f2cd51",
         "00000000000000cd0c88000080000000871101ed7f8cb47a4eea086324a7f9dd7427809701008000",
         "97f54bae49aa71f8"),
        ("7962d9ece03d1acd4c76089dce131543",
         "46b9342a41396cd7386bf5803104d7cedc122b9132139baf2eedc94ee178534f8000000000000000",
         "2f2d235d074d7449"),
        ("d6c47ff4677ac8ae91cb49f4ce673432",
         "9646c154bfb7be7c0c86000080000000874901a9c6bf370c62aa43ca1ec9b97071727654822361deb8ba4e5719aa05d8e86aa36164dfa5e506dbf60dc7418858179fd25dac5e9e49393dde37e29652444941b562142ab5f19aea919701008000",
         "5083316637060c4d"),
        ("c1bc1f075797b970b5a45e64a764b0cb",
         "7f494f060a04007f000702020402018641042dbaf62e6fdfb31eb66f206493b9e7721586f0e5c93754d7bd5a884ca251ee4d720ab539a60561bc46812fa289b58ac69f0c6e32adbaf7241049a31211f80f5b800000000000",
         "8d7c617d43efe09e"),
    ]
    for key, data, expected in vectors:
        assert crypto.retail_mac(
            bytes.fromhex(key), bytes.fromhex(data)
        ).hex() == expected


def test_derived_keys_are_16_bytes_and_odd_parity():
    k_enc, k_mac, info = crypto.derive_keys("L898902C3", "740812", "120415")
    assert len(k_enc) == 16 and len(k_mac) == 16
    assert info == "L898902C3674081221204159"
    for key in (k_enc, k_mac):
        for byte in key:
            assert bin(byte).count("1") % 2 == 1
