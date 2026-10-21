// cmake-consumer: an application linking teta::wot, found with find_package.
//
// It writes a JSON value the way teta-wot does, to show the library is linked
// and works. As the library grows, this stays the smallest program using it.

#include <iostream>
#include <teta/wot.hpp>

int main() {
    namespace json = teta::wot::json;
    const json::Value value = {
        {"library", "teta-wot-cpp"}, {"version", teta::wot::version}, {"pi", 3.14159}, {"big", 1e16}};
    const auto text = json::dump(value);
    std::cout << text << '\n';
    // serde_json's formatting: a signed exponent for 1e16.
    return text.find("\"big\":1e+16") == std::string::npos ? 1 : 0;
}
