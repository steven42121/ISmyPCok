#ifndef ISPCOK_CORE_JSON_WRITER_H
#define ISPCOK_CORE_JSON_WRITER_H

#include "core/types.h"

#include <string>

namespace ispcok {

std::string ReportToJson(const RunReport& report);

} // namespace ispcok

#endif
