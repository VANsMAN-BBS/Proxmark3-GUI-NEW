from emrtd_pm3 import mrz

TD3 = (
    "P<UTOERIKSSON<<ANNA<MARIA<<<<<<<<<<<<<<<<<<<\n"
    "L898902C36UTO7408122F1204159ZE184226B<<<<<10"
)

TD2 = (
    "I<UTOERIKSSON<<ANNA<MARIA<<<<<<<<<<<\n"
    "D231458907UTO7408122F1204159<<<<<<<6"
)

TD1 = (
    "I<UTOD231458907<<<<<<<<<<<<<<<\n"
    "7408122F1204159UTO<<<<<<<<<<<6\n"
    "ERIKSSON<<ANNA<MARIA<<<<<<<<<<"
)


def test_td3():
    result = mrz.parse_mrz(TD3)
    assert result["layout"] == "TD3"
    assert result["document_number"] == "L898902C3"
    assert result["date_of_birth"] == "740812"
    assert result["date_of_expiry"] == "120415"
    assert result["sex"] == "F"
    assert result["nationality"] == "UTO"
    assert result["surname"] == "ERIKSSON"
    assert result["given_names"] == "ANNA MARIA"
    assert all(result["checks"].values())


def test_td2():
    result = mrz.parse_mrz(TD2)
    assert result["layout"] == "TD2"
    assert result["document_number"] == "D23145890"
    assert result["surname"] == "ERIKSSON"
    assert all(result["checks"].values())


def test_td1():
    result = mrz.parse_mrz(TD1)
    assert result["layout"] == "TD1"
    assert result["document_number"] == "D23145890"
    assert result["surname"] == "ERIKSSON"
    assert all(result["checks"].values())


def test_national_flat_layout():
    text = (
        "CSCJ12345670<3601062<0602161<6MHABCDEFTEST<<PERSON<<<<<<<<"
        "MAAC000000<<<<<<<<<<<<<<<<<<<<"
    )
    result = mrz.parse_mrz(text)
    assert result["layout"] == "national"
    assert result["document_number"] == "CJ1234567"
    assert result["date_of_birth"] == "060216"
    assert result["date_of_expiry"] == "360106"
    assert "note" in result
