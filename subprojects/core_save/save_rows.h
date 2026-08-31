/// @file
/// @brief The row codecs: one write/read pair per saved table.
/// @threading SINGLE_THREADED
/// Internal to core_save; called from the single encode/decode path.
///
/// EACH PAIR SITS TOGETHER on purpose. The one failure this format cannot
/// detect at run time is a field added to a row and to the writer but not
/// to the reader (or the other way round): the file would be well-formed
/// and wrong. Two functions ten lines apart make that omission visible in
/// the diff; a sizeof tripwire in the .cpp catches the case where neither
/// was touched at all (manual/67-save-format.md §7).
///
/// Fields are written in DECLARATION ORDER of the header that defines the
/// row. That is the one ordering rule of the format, and it is what makes
/// the codec reviewable against the state header side by side.

#ifndef CORE_SAVE_SAVE_ROWS_H_
#define CORE_SAVE_SAVE_ROWS_H_

#include "core_common/family_state.h"
#include "core_common/herd_state.h"
#include "core_common/land_state.h"
#include "core_common/order_state.h"
#include "core_common/resident_state.h"
#include "core_common/unit_state.h"
#include "save_dictionary.h"

namespace core {

void WriteResidentRow(SaveSink& sink, const ResidentRow& row);
ResidentRow ReadResidentRow(LoadSource& source);

void WriteFamilyRow(SaveSink& sink, const FamilyRow& row);
FamilyRow ReadFamilyRow(LoadSource& source);

void WriteFieldRow(SaveSink& sink, const FieldRow& row);
FieldRow ReadFieldRow(LoadSource& source);

void WriteUnitRow(SaveSink& sink, const UnitRow& row);
UnitRow ReadUnitRow(LoadSource& source);

void WriteHerdRow(SaveSink& sink, const HerdRow& row);
HerdRow ReadHerdRow(LoadSource& source);

void WriteOrderRow(SaveSink& sink, const OrderRow& row);
OrderRow ReadOrderRow(LoadSource& source);

}  // namespace core

#endif  // CORE_SAVE_SAVE_ROWS_H_
