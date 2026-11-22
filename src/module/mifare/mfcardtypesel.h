#ifndef MFCARDTYPESEL_H
#define MFCARDTYPESEL_H

#include "mfgen3types.h"

#include <QString>

namespace mfcardtype
{

/**
 * Priority between the automatic card layout (SAK/hf mf info) and the user's
 * manual choice in the card-type combo box.
 *
 *  - A confirmed detection for a *new* card always wins and clears the manual
 *    lock.
 *  - While the same card is present, a manual choice is kept even if a later
 *    refresh reports the same confirmed layout.
 *  - Suspected/unknown identities never change the table layout.
 */
struct SelectionState
{
    int type = 1;                 ///< Mifare::card_1k
    QString lastAutoUid;
    bool userLocked = false;
};

/** Map a read-only identity to the existing card type id (0/1/2/4), or -1. */
inline int typeIdForIdentity(const mfgen3::CardIdentity &identity)
{
    switch(identity.kind)
    {
    case mfgen3::CardKind::ClassicMini:
        return 0;
    case mfgen3::CardKind::Classic1K:
    case mfgen3::CardKind::FM11RF08S:
        return 1;
    case mfgen3::CardKind::Classic4K:
        return 4;
    default:
        return -1;
    }
}

/**
 * Apply a detected type.  Returns true when the caller must change the combo
 * box and the data/key layout; false means the current selection stays.
 */
inline bool applyDetected(SelectionState *state, int detectedType,
                          const QString &uid, bool confirmed)
{
    if(!state || detectedType < 0 || !confirmed)
        return false;

    if(uid != state->lastAutoUid)
    {
        // A different card clears the manual override: the new card's layout
        // is more important than a choice made for the previous card.
        state->lastAutoUid = uid;
        state->userLocked = false;
    }
    else if(state->userLocked)
    {
        // Same card, user chose a layout: an unrelated refresh must not undo it.
        return false;
    }

    const bool changed = state->type != detectedType;
    state->type = detectedType;
    return changed;
}

inline void selectManually(SelectionState *state, int type)
{
    if(!state)
        return;
    state->type = type;
    state->userLocked = true;
}

} // namespace mfcardtype

#endif // MFCARDTYPESEL_H
