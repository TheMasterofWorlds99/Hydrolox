#include <types.hpp>

bool TypeInfo::operator==(const TypeInfo &other) const {
  if (data.index() != other.data.index())
    return false;

  return std::visit(
      overloaded{[&](const PrimitiveType &a) {
                   return a.tag == std::get<PrimitiveType>(other.data).tag;
                 },
                 [&](const VectorType &a) {
                   auto &b = std::get<VectorType>(other.data);
                   return a.size == b.size && (*a.base == *b.base);
                 },
                 [&](const ArrayType &a) {
                   auto &b = std::get<ArrayType>(other.data);
                   return a.size == b.size && (*a.base == *b.base);
                 },
                 [&](const StructType &a) {
                   return a.name == std::get<StructType>(other.data).name;
                 }},
      data);
}
