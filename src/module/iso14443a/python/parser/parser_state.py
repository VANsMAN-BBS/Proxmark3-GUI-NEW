#!/usr/bin/env python3
# -*- coding: utf-8 -*-

"""Per-capture parser state.

The Stage-1 parser originally kept its protocol state machine in class-level
attributes on :class:`ISO14443AParser`.  That works for one interactive GUI
session but makes two captures (for example two consecutive PM3 traces) share
their T=CL / Mifare / native-card state.

``ParserState`` is the same set of fields in one plain object.  The parser keeps
its public class-level attribute names as a compatibility facade, but every
read/write is routed to an active ``ParserState`` instance.  A caller that
wants isolation creates a new state and uses
``ISO14443AParser.parser_state(state)`` around one capture.

No parsing logic lives here: this file only owns the mutable session data.
"""


class ParserState(object):
    """Mutable protocol/session state for one capture."""

    # Field names are intentionally the historical underscore names so the
    # compatibility facade on ISO14443AParser can proxy them unchanged.
    FIELDS = frozenset((
        # ISO 14443-4 / T=CL
        "_tcl_active",
        "_waiting_for_ats",
        # MIFARE Classic session
        "_mifare_active",
        "_mifare_auth_step",
        "_mifare_last_cmd",
        "_mifare_last_block",
        "_mifare_pending_data",
        "_mifare_data_sent",
        "_mifare_probe_pending",
        # NTAG / Ultralight native session
        "_native_active",
        "_native_last_cmd",
        "_native_last_page",
        # Card identification
        "_last_atqa",
        "_last_sak",
        "_last_card_type",
        "_last_ats",
        # UID cascade (CL1/CL2/CL3)
        "_last_sel_code",
        "_uid_cl1",
        "_uid_cl2",
        "_uid_cl3",
        "_uid_full",
    ))

    def __init__(self):
        self.reset()

    def reset(self):
        """Clear every field (new capture / reset request)."""
        self._tcl_active = False
        self._waiting_for_ats = False

        self._mifare_active = False
        self._mifare_auth_step = 0
        self._mifare_last_cmd = None
        self._mifare_last_block = None
        self._mifare_pending_data = False
        self._mifare_data_sent = False
        self._mifare_probe_pending = False

        self._native_active = False
        self._native_last_cmd = None
        self._native_last_page = None

        self._last_atqa = None
        self._last_sak = None
        self._last_card_type = None
        self._last_ats = None

        self._last_sel_code = None
        self._uid_cl1 = None
        self._uid_cl2 = None
        self._uid_cl3 = None
        self._uid_full = None
        return self

    def reset_mifare(self):
        self._mifare_active = False
        self._mifare_auth_step = 0
        self._mifare_last_cmd = None
        self._mifare_last_block = None
        self._mifare_pending_data = False
        self._mifare_data_sent = False
        self._mifare_probe_pending = False
        return self

    def reset_native(self):
        self._native_active = False
        self._native_last_cmd = None
        self._native_last_page = None
        return self

    def snapshot(self):
        """Return a JSON-friendly snapshot (never invents values)."""
        uid = self._uid_full
        ats = self._last_ats
        return {
            "tcl_active": bool(self._tcl_active),
            "waiting_for_ats": bool(self._waiting_for_ats),
            "mifare_active": bool(self._mifare_active),
            "mifare_auth_step": int(self._mifare_auth_step or 0),
            "native_active": bool(self._native_active),
            "atqa": self._last_atqa,
            "sak": self._last_sak,
            "card_type": self._last_card_type,
            "uid": bytes(uid).hex().upper() if uid else None,
            "uid_len": len(uid) if uid else None,
            "ats": dict(ats) if ats else None,
        }
