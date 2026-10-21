#include "teta/wot/td/constraints.hpp"

#include <cmath>
#include <string_view>
#include <vector>

namespace teta::wot::td {

namespace {

// Whether the constraint `name` suits values of `type`.
bool suits(std::string_view name, std::optional<DataType> type) {
    if (name == "gt" || name == "ge" || name == "lt" || name == "le" || name == "multiple_of") {
        return type == DataType::integer || type == DataType::number;
    }
    if (name == "allow_inf_nan") {
        return type == DataType::number;
    }
    if (name == "min_length" || name == "max_length") {
        return type == DataType::string || type == DataType::array || type == DataType::object;
    }
    if (name == "pattern") {
        return type == DataType::string;
    }
    return false;
}

std::vector<const char*> set_names(const Constraints& c) {
    std::vector<const char*> names;
    if (c.gt_) names.push_back("gt");
    if (c.ge_) names.push_back("ge");
    if (c.lt_) names.push_back("lt");
    if (c.le_) names.push_back("le");
    if (c.multiple_of_) names.push_back("multiple_of");
    if (c.allow_inf_nan_) names.push_back("allow_inf_nan");
    if (c.min_length_) names.push_back("min_length");
    if (c.max_length_) names.push_back("max_length");
    if (c.pattern_) names.push_back("pattern");
    return names;
}

void check_numeric_bounds(const Constraints& c) {
    for (const auto& [name, bound] : {std::pair{"gt", c.gt_}, std::pair{"ge", c.ge_}, std::pair{"lt", c.lt_},
                                      std::pair{"le", c.le_}, std::pair{"multiple_of", c.multiple_of_}}) {
        if (bound) {
            bound->to_number(name);
        }
    }
}

// Writes the constraints into a schema they are known to suit.
void write(const Constraints& c, DataSchema& schema) {
    if (c.gt_) schema.exclusive_minimum = c.gt_->to_number("gt");
    if (c.ge_) schema.minimum = c.ge_->to_number("ge");
    if (c.lt_) schema.exclusive_maximum = c.lt_->to_number("lt");
    if (c.le_) schema.maximum = c.le_->to_number("le");
    if (c.multiple_of_) schema.multiple_of = c.multiple_of_->to_number("multiple_of");
    if (c.pattern_) schema.pattern = c.pattern_;
    for (const auto& [length, is_min] : {std::pair{c.min_length_, true}, std::pair{c.max_length_, false}}) {
        if (!length) {
            continue;
        }
        if (schema.data_type == DataType::string) {
            (is_min ? schema.min_length : schema.max_length) = *length;
        } else if (schema.data_type == DataType::array) {
            (is_min ? schema.min_items : schema.max_items) = *length;
        } else {
            schema.extra[is_min ? "minProperties" : "maxProperties"] = *length;
        }
    }
}

}  // namespace

double Bound::as_double() const {
    return std::visit([](auto v) { return static_cast<double>(v); }, value_);
}

bool Bound::non_positive() const {
    if (const auto* i = std::get_if<std::int64_t>(&value_)) {
        return *i <= 0;
    }
    if (std::holds_alternative<std::uint64_t>(value_)) {
        return false;
    }
    return std::get<double>(value_) <= 0.0;
}

json::Value Bound::to_number(const char* constraint) const {
    if (const auto* i = std::get_if<std::int64_t>(&value_)) {
        return *i;
    }
    if (const auto* u = std::get_if<std::uint64_t>(&value_)) {
        return *u;
    }
    const double d = std::get<double>(value_);
    if (!std::isfinite(d)) {
        throw ConstraintError(ConstraintError::Kind::not_finite,
                              std::string("constraint `") + constraint + "` must be finite");
    }
    return d;
}

void Constraints::apply(DataSchema& schema) const {
    if (empty()) {
        return;
    }
    if (multiple_of_ && multiple_of_->non_positive()) {
        throw ConstraintError(ConstraintError::Kind::not_positive, "`multiple_of` must be greater than zero");
    }
    if (schema.data_type) {
        for (const char* name : set_names(*this)) {
            if (!suits(name, schema.data_type)) {
                throw ConstraintError(
                    ConstraintError::Kind::unsuitable,
                    std::string("constraint `") + name + "` doesn't suit " + to_string(*schema.data_type));
            }
        }
        check_numeric_bounds(*this);
        write(*this, schema);
        return;
    }
    if (!schema.one_of) {
        throw ConstraintError(ConstraintError::Kind::untyped,
                              "constraints need a schema with a `type` or a `oneOf`");
    }
    auto& branches = *schema.one_of;
    for (const char* name : set_names(*this)) {
        bool any = false;
        for (const auto& branch : branches) {
            any = any || suits(name, branch.data_type);
        }
        if (!any) {
            throw ConstraintError(
                ConstraintError::Kind::unsuitable,
                std::string("constraint `") + name + "` doesn't suit any of the schema's types");
        }
    }
    check_numeric_bounds(*this);
    for (auto& branch : branches) {
        const auto type = branch.data_type;
        Constraints only;
        if (suits("gt", type)) only.gt_ = gt_;
        if (suits("ge", type)) only.ge_ = ge_;
        if (suits("lt", type)) only.lt_ = lt_;
        if (suits("le", type)) only.le_ = le_;
        if (suits("multiple_of", type)) only.multiple_of_ = multiple_of_;
        if (suits("allow_inf_nan", type)) only.allow_inf_nan_ = allow_inf_nan_;
        if (suits("min_length", type)) only.min_length_ = min_length_;
        if (suits("max_length", type)) only.max_length_ = max_length_;
        if (suits("pattern", type)) only.pattern_ = pattern_;
        write(only, branch);
    }
}

}  // namespace teta::wot::td
