#ifndef FELICA_CJRC_PROFILE_H
#define FELICA_CJRC_PROFILE_H

#include "transit_types.h"

/**
 * CJRC transit profile (Suica / PASMO / ICOCA compatible, system 0x0003).
 *
 * Only public, unencrypted services are read:
 *   0x008B attribute (balance / transaction counter)
 *   0x090F transaction history (20 blocks)
 *   0x108F gate entry/exit history (3 blocks)
 *   0x10CB last SF gate entry (2 blocks)
 */
class CjrcProfile : public Transit::TransitCardProfile
{
public:
    QString profileId() const override;
    QString displayName() const override;
    QString currency() const override;

    bool matches(const Transit::CardInfo &card) const override;
    QVector<Transit::ServiceDefinition> services() const override;

    bool parse(const QVector<Transit::RawBlock> &blocks,
               Transit::ReadResult &out,
               QStringList &warnings) const override;

    // Field decoders, exposed for tests and for the raw-data view.
    static QString decodeDate(quint16 value);
    static QString decodeTime(quint16 value);
    static Transit::StationInfo resolveStation(int line, int station, int area = -1);
};

#endif // FELICA_CJRC_PROFILE_H
