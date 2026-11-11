import pytest

from emrtd_pm3 import card, dg, mrz, tlv
from tests import specimens
from tests.fake_chip import FakeChip


def make_reader(files=None) -> card.EMRTD:
    chip = FakeChip(
        specimens.ICAO_TD3_DOC,
        specimens.ICAO_TD3_DOB,
        specimens.ICAO_TD3_EXP,
        files or specimens.sample_files(),
    )
    reader = card.EMRTD(chip)
    reader.select_aid()
    reader.authenticate(
        specimens.ICAO_TD3_DOC, specimens.ICAO_TD3_DOB, specimens.ICAO_TD3_EXP
    )
    return reader


def test_full_read():
    reader = make_reader()

    com = dg.parse_ef_com(reader.read_ef_com())
    assert com["data_groups"] == [1, 2, 11]
    assert com["lds_version"] == "1.7"

    document = mrz.parse_mrz(dg.parse_dg1(reader.read_dg("DG1")))
    assert document["document_number"] == "L898902C3"
    assert document["surname"] == "ERIKSSON"
    assert document["given_names"] == "ANNA MARIA"

    face = dg.extract_face(reader.read_dg("DG2"))
    assert face["format"] == "jpeg"
    assert face["data"].startswith(b"\xff\xd8\xff")

    extra = dg.parse_dg11(reader.read_dg("DG11"))
    assert extra["name_of_holder"] == "TEST PERSON"
    assert extra["name_of_holder_mrz"].startswith("TEST<<PERSON")


def test_large_file_is_chunked():
    large = tlv.encode(0x63, bytes(range(256)) * 6)  # ~1.5 KB
    files = specimens.sample_files()
    files[0x0103] = large

    reader = make_reader(files)
    assert reader.read_dg("DG3") == large


def test_wrong_mrz_is_reported_with_retries():
    chip = FakeChip(
        specimens.ICAO_TD3_DOC,
        specimens.ICAO_TD3_DOB,
        specimens.ICAO_TD3_EXP,
        specimens.sample_files(),
    )
    reader = card.EMRTD(chip)
    reader.select_aid()
    with pytest.raises(card.AuthenticationError) as info:
        reader.authenticate(specimens.ICAO_TD3_DOC, "000000", specimens.ICAO_TD3_EXP)
    assert info.value.retries == 15


def test_missing_file_raises():
    reader = make_reader()
    with pytest.raises(card.EMRTDError):
        reader.read_dg("DG12")
