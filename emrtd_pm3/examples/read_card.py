"""Minimal example: read one document and print a few fields."""

from emrtd_pm3 import EMRTD, Proxmark3, dg, mrz

PORT = "COM14"
DOC = "C12345678"
DOB = "900101"
EXP = "300101"

with Proxmark3(PORT) as pm3:
    card = EMRTD(pm3)
    card.select_aid()
    card.authenticate(DOC, DOB, EXP)

    print("EF.COM:", dg.parse_ef_com(card.read_ef_com()))
    print("DG1  :", mrz.parse_mrz(dg.parse_dg1(card.read_dg("DG1"))))

    face = dg.extract_face(card.read_dg("DG2"))
    if face:
        print("face :", face["format"], len(face["data"]), "bytes")
