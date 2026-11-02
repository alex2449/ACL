#pragma once

#include <memory>
#include <string>
#include <variant>
#include <vector>

bool isAsciiPrintable(char c);

std::string charValueToLiteral(char c);

std::string stringValueToLiteralWithoutQuotes(const std::string &s);
std::string stringValueToLiteral(const std::string &s);

std::string ordinal(int n);

template <typename T>
std::vector<T *> view(const std::vector<std::unique_ptr<T>> &ptrs) {
  std::vector<T *> result;
  result.reserve(ptrs.size());

  for (const auto &ptr : ptrs)
    result.push_back(ptr.get());

  return result;
}

template <typename... Fs>
struct overloaded : Fs... { using Fs::operator()...; };
template <typename... Fs>
overloaded(Fs...) -> overloaded<Fs...>;

template <typename Derived, typename... Ts>
class variant : public std::variant<Ts...> {
 public:
  using std::variant<Ts...>::variant;

  template <typename T> bool is() const { return std::holds_alternative<T>(*this); }
  template <typename T> T &get() { return std::get<T>(*this); }
  template <typename T> const T &get() const { return std::get<T>(*this); }
  template <typename T> T *get_if() { return std::get_if<T>(this); }
  template <typename T> const T *get_if() const { return std::get_if<T>(this); }

  template <typename... Fs> decltype(auto) visit(Fs &&...fs) {
    return std::visit(overloaded{std::forward<Fs>(fs)...}, static_cast<std::variant<Ts...> &>(*this));
  }

  template <typename... Fs> decltype(auto) visit(Fs &&...fs) const {
    return std::visit(overloaded{std::forward<Fs>(fs)...}, static_cast<const std::variant<Ts...> &>(*this));
  }

  template <typename D = Derived>
  typename D::Tag getTag() const {
    return static_cast<typename D::Tag>(this->index());
  }
};
