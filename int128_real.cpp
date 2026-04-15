#include <algorithm>
#include <cstdint>
#include <iomanip>
#include <iostream>
#include <map>
#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

class Int128 {
public:
    // конструктор
    Int128() : lo_(0), hi_(0) {}

    Int128(int64_t value)
        : lo_(static_cast<uint64_t>(value)),
        hi_(value < 0 ? -1 : 0) {
    }
    // это младшие и страшие 64 бита чисоа => low and high

    explicit Int128(std::string_view sv) {
        from_string(sv);
    }

    // явно приводим к инт64
    explicit operator int64_t() const {
        if (hi_ == 0 && lo_ <= static_cast<uint64_t>(INT64_MAX))
            return static_cast<int64_t>(lo_);
        if (hi_ == -1 && lo_ >= static_cast<uint64_t>(INT64_MIN))
            return static_cast<int64_t>(lo_);
        return static_cast<int64_t>(lo_);
    }

    // явно приводим к дабл
    explicit operator double() const {
        double result = static_cast<double>(hi_) * (static_cast<double>(UINT64_MAX) + 1.0);
        result += static_cast<double>(lo_);
        return result;
    }

    // в строку
    std::string str() const {
        if (is_zero())
            return "0";

        // через модуль
        bool negative = is_negative();
        Int128 abs_val = negative ? -*this : *this;

        std::string result;
        Int128 ten(10);

        while (!abs_val.is_zero()) {
            Int128 quotient, remainder;
            div_mod(abs_val, ten, quotient, remainder);
            int digit = static_cast<int>(remainder.lo_);
            result.push_back('0' + digit);
            abs_val = quotient;
        }

        if (negative)
            result.push_back('-');

        std::reverse(result.begin(), result.end());
        return result;
    }

    // арифметика:
    Int128 operator+(const Int128& other) const {
        uint64_t lo = lo_ + other.lo_;
        uint64_t carry = (lo < lo_) ? 1 : 0;
        int64_t hi = hi_ + other.hi_ + carry;
        return Int128(lo, hi);
    }

    Int128 operator-(const Int128& other) const {
        Int128 neg_other = -other;
        return *this + neg_other;
    }

    // работу с битами подсмотрел у иишки простите
    Int128 operator*(const Int128& other) const {
        bool sign = is_negative() ^ other.is_negative();

        Int128 a = abs();
        Int128 b = other.abs();

        // Умножение беззнаковых 128-битных чисел
        uint64_t a_lo = a.lo_, a_hi = static_cast<uint64_t>(a.hi_);
        uint64_t b_lo = b.lo_, b_hi = static_cast<uint64_t>(b.hi_);

        // Разбиваем на 32-битные части для умножения
        uint64_t a_lo_lo = a_lo & 0xFFFFFFFF;
        uint64_t a_lo_hi = a_lo >> 32;
        uint64_t b_lo_lo = b_lo & 0xFFFFFFFF;
        uint64_t b_lo_hi = b_lo >> 32;

        // Произведения частей
        uint64_t p1 = a_lo_lo * b_lo_lo;
        uint64_t p2 = a_lo_lo * b_lo_hi;
        uint64_t p3 = a_lo_hi * b_lo_lo;
        uint64_t p4 = a_lo_hi * b_lo_hi;

        // Суммируем с переносами
        uint64_t carry;
        uint64_t lo = p1;
        carry = (lo >> 32);
        lo &= 0xFFFFFFFF;

        uint64_t mid = (p2 & 0xFFFFFFFF) + (p3 & 0xFFFFFFFF) + carry;
        carry = mid >> 32;
        mid &= 0xFFFFFFFF;

        uint64_t hi = (p2 >> 32) + (p3 >> 32) + p4 + carry;

        // Добавляем вклад от a_hi и b_hi
        hi += a_hi * b_lo + b_hi * a_lo;
        // Старшая часть 128-битного произведения (в дополнительном коде для беззнакового)
        // Но нам нужно 128 бит, старшая часть hi уже содержит 64 бита, остальное отбросим (переполнение)

        // Собираем результат
        Int128 res(lo, static_cast<int64_t>(hi));
        return sign ? -res : res;

        // поясню: разбиваем на еще болнее мелкие кусочки,
        // чтобы не было переполнения в промежуточных вычислениях
    }

    Int128 operator/(const Int128& other) const {
        bool sign = is_negative() ^ other.is_negative();

        Int128 a = abs();
        Int128 b = other.abs();

        Int128 quotient, remainder;
        div_mod(a, b, quotient, remainder);

        return sign ? -quotient : quotient;
    }

    Int128 operator-() const {
        // работа с дополнительным кодом числа + 1
        uint64_t lo = ~lo_ + 1;
        uint64_t carry = (lo == 0) ? 1 : 0;
        int64_t hi = ~hi_ + carry;
        return Int128(lo, hi);
    }

    Int128& operator+=(const Int128& other) {
        *this = *this + other;
        return *this;
    }

    Int128& operator-=(const Int128& other) {
        *this = *this - other;
        return *this;
    }

    Int128& operator*=(const Int128& other) {
        *this = *this * other;
        return *this;
    }

    Int128& operator/=(const Int128& other) {
        *this = *this / other;
        return *this;
    }

    // сравнения
    bool operator==(const Int128& other) const {
        return lo_ == other.lo_ && hi_ == other.hi_;
    }

    bool operator!=(const Int128& other) const {
        return !(*this == other);
    }

    // поточный вывод << >>
    friend std::ostream& operator<<(std::ostream& os, const Int128& value) {
        os << value.str();
        return os;
    }

private:
    uint64_t lo_;
    int64_t hi_;  // знаковая старшая часть (дополнительный код)

    Int128(uint64_t lo, int64_t hi) : lo_(lo), hi_(hi) {}

    bool is_zero() const {
        return lo_ == 0 && hi_ == 0;
    }

    bool is_negative() const {
        return hi_ < 0;
    }

    Int128 abs() const {
        return is_negative() ? -*this : *this;
    }

    // парсинг
    void from_string(std::string_view sv) {
        bool negative = false;
        if (!sv.empty() && sv[0] == '-') {
            negative = true;
            sv.remove_prefix(1);
        }
        *this = Int128(0);
        Int128 ten(10);
        for (char ch : sv) {
            if (ch < '0' || ch > '9')
                break;
            int digit = ch - '0';
            *this = *this * ten + Int128(digit);
        }
        if (negative)
            *this = -*this;
    }

    // беззнаковое деление 128-битных чисел построенное на инт64
    static void div_mod(const Int128& dividend, const Int128& divisor,
        Int128& quotient, Int128& remainder) {
        // пусть оба положительны
        if (divisor.is_zero()) {
            quotient = Int128(0);
            remainder = Int128(0);
            return;
        }

        // реализация алгоритма двоичного деления (тоже украл)
        quotient = Int128(0);
        remainder = Int128(0);

        for (int i = 127; i >= 0; --i) {
            remainder = remainder << 1;
            // берём i-й бит делимого
            uint64_t bit = (dividend.hi_ >= 0 ?
                (static_cast<uint64_t>(dividend.hi_) >> (i - 64)) :
                // для отрицательных чисел не попадаем, т.к. abs()
                0);
            if (i >= 64) {
                int shift = i - 64;
                if (dividend.hi_ & (static_cast<uint64_t>(1) << shift))
                    remainder.lo_ |= 1;
            }
            else {
                if (dividend.lo_ & (static_cast<uint64_t>(1) << i))
                    remainder.lo_ |= 1;
            }

            if (remainder >= divisor) {
                remainder = remainder - divisor;
                if (i >= 64)
                    quotient.hi_ |= (static_cast<uint64_t>(1) << (i - 64));
                else
                    quotient.lo_ |= (static_cast<uint64_t>(1) << i);
            }
        }
    }

    // операторы сравнения
    bool operator>=(const Int128& other) const {
        if (hi_ != other.hi_)
            return static_cast<uint64_t>(hi_) > static_cast<uint64_t>(other.hi_);
        return lo_ >= other.lo_;
    }

    Int128 operator<<(int shift) const {
        if (shift == 0) return *this;
        if (shift >= 128) return Int128(0);
        uint64_t lo = 0, hi = 0;
        if (shift < 64) {
            lo = lo_ << shift;
            hi = (hi_ << shift) | (lo_ >> (64 - shift));
        }
        else {
            lo = 0;
            hi = lo_ << (shift - 64);
        }
        return Int128(lo, static_cast<int64_t>(hi));
    }
};


// пред объявление
class Expression;

class Expression {
public:
    virtual ~Expression() = default;

    // вычисление значеия
    virtual Int128 eval(const std::map<std::string, Int128>& vars) const = 0;

    // создание клоуна-указателя
    virtual std::unique_ptr<Expression> clone() const = 0;

    // вывод
    virtual void print(std::ostream& os) const = 0;

    friend std::ostream& operator<<(std::ostream& os, const Expression& expr) {
        expr.print(os);
        return os;
    }
};

// константное
class Const : public Expression {
public:
    explicit Const(Int128 value) : value_(value) {}

    Int128 eval(const std::map<std::string, Int128>&) const override {
        return value_;
    }

    std::unique_ptr<Expression> clone() const override {
        return std::make_unique<Const>(value_);
    }

    void print(std::ostream& os) const override {
        os << value_;
    }

private:
    Int128 value_;
};

// переменная
class Variable : public Expression {
public:
    explicit Variable(std::string name) : name_(std::move(name)) {}

    Int128 eval(const std::map<std::string, Int128>& vars) const override {
        auto it = vars.find(name_);
        // Если переменная не найдена, возвращаем 0
        return it != vars.end() ? it->second : Int128(0);
    }

    std::unique_ptr<Expression> clone() const override {
        return std::make_unique<Variable>(name_);
    }

    void print(std::ostream& os) const override {
        os << name_;
    }

private:
    std::string name_;
};

// унарный минус
class Negate : public Expression {
public:
    explicit Negate(std::unique_ptr<Expression> operand)
        : operand_(std::move(operand)) {
    }

    explicit Negate(const Expression& operand)
        : operand_(operand.clone()) {
    }

    Int128 eval(const std::map<std::string, Int128>& vars) const override {
        return -operand_->eval(vars);
    }

    std::unique_ptr<Expression> clone() const override {
        return std::make_unique<Negate>(operand_->clone());
    }

    void print(std::ostream& os) const override {
        os << "-(" << *operand_ << ")";
    }

private:
    std::unique_ptr<Expression> operand_;
};

// сложение
class Add : public Expression {
public:
    Add(std::unique_ptr<Expression> left, std::unique_ptr<Expression> right)
        : left_(std::move(left)), right_(std::move(right)) {
    }

    Add(const Expression& left, const Expression& right)
        : left_(left.clone()), right_(right.clone()) {
    }

    Int128 eval(const std::map<std::string, Int128>& vars) const override {
        return left_->eval(vars) + right_->eval(vars);
    }

    std::unique_ptr<Expression> clone() const override {
        return std::make_unique<Add>(left_->clone(), right_->clone());
    }

    void print(std::ostream& os) const override {
        os << "(" << *left_ << " + " << *right_ << ")";
    }

private:
    std::unique_ptr<Expression> left_;
    std::unique_ptr<Expression> right_;
};

// вычтание
class Subtract : public Expression {
public:
    Subtract(std::unique_ptr<Expression> left, std::unique_ptr<Expression> right)
        : left_(std::move(left)), right_(std::move(right)) {
    }

    Subtract(const Expression& left, const Expression& right)
        : left_(left.clone()), right_(right.clone()) {
    }

    Int128 eval(const std::map<std::string, Int128>& vars) const override {
        return left_->eval(vars) - right_->eval(vars);
    }

    std::unique_ptr<Expression> clone() const override {
        return std::make_unique<Subtract>(left_->clone(), right_->clone());
    }

    void print(std::ostream& os) const override {
        os << "(" << *left_ << " - " << *right_ << ")";
    }

private:
    std::unique_ptr<Expression> left_;
    std::unique_ptr<Expression> right_;
};

// умножение
class Multiply : public Expression {
public:
    Multiply(std::unique_ptr<Expression> left, std::unique_ptr<Expression> right)
        : left_(std::move(left)), right_(std::move(right)) {
    }

    Multiply(const Expression& left, const Expression& right)
        : left_(left.clone()), right_(right.clone()) {
    }

    Int128 eval(const std::map<std::string, Int128>& vars) const override {
        return left_->eval(vars) * right_->eval(vars);
    }

    std::unique_ptr<Expression> clone() const override {
        return std::make_unique<Multiply>(left_->clone(), right_->clone());
    }

    void print(std::ostream& os) const override {
        os << "(" << *left_ << " * " << *right_ << ")";
    }

private:
    std::unique_ptr<Expression> left_;
    std::unique_ptr<Expression> right_;
};

// деление
class Divide : public Expression {
public:
    Divide(std::unique_ptr<Expression> left, std::unique_ptr<Expression> right)
        : left_(std::move(left)), right_(std::move(right)) {
    }

    Divide(const Expression& left, const Expression& right)
        : left_(left.clone()), right_(right.clone()) {
    }

    Int128 eval(const std::map<std::string, Int128>& vars) const override {
        return left_->eval(vars) / right_->eval(vars);
    }

    std::unique_ptr<Expression> clone() const override {
        return std::make_unique<Divide>(left_->clone(), right_->clone());
    }

    void print(std::ostream& os) const override {
        os << "(" << *left_ << " / " << *right_ << ")";
    }

private:
    std::unique_ptr<Expression> left_;
    std::unique_ptr<Expression> right_;
};


Add operator+(const Expression& lhs, const Expression& rhs) {
    return Add(lhs, rhs);
}

Subtract operator-(const Expression& lhs, const Expression& rhs) {
    return Subtract(lhs, rhs);
}

Multiply operator*(const Expression& lhs, const Expression& rhs) {
    return Multiply(lhs, rhs);
}

Divide operator/(const Expression& lhs, const Expression& rhs) {
    return Divide(lhs, rhs);
}

Negate operator-(const Expression& operand) {
    return Negate(operand);
}


int main() {
    // 1) Конструирование
    Int128 a;                         // по умолчанию (обычно 0)
    Int128 b(int64_t{ -42 });           // из int64_t
    Int128 c(std::string_view("123456789012345678901234567890")); // из строки

    // 2) Преобразование в строку
    std::cout << "a=" << a.str() << "\n";
    std::cout << "b=" << b.str() << "\n";
    std::cout << "c=" << c.str() << "\n";

    // 3) Вывод в поток (operator<<)
    std::cout << "c (via <<) = " << c << "\n";

    // 4) Арифметика (+, -, *, /) и составные (+=, -=, *=, /=)
    Int128 x("100000000000000000000");   // 1e20
    Int128 y("3000000000000000000");     // 3e18

    Int128 sum = x + y;
    Int128 diff = x - y;
    Int128 prod = x * y;
    Int128 quot = x / y;                 // целочисленное деление

    std::cout << "sum=" << sum << "\n";
    std::cout << "diff=" << diff << "\n";
    std::cout << "prod=" << prod << "\n";
    std::cout << "quot=" << quot << "\n";

    // Составные операторы
    Int128 t("10");
    t += Int128("5");    // 15
    t *= Int128("7");    // 105
    t -= Int128("20");   // 85
    t /= Int128("4");    // 21 (если деление как в C++: trunc toward zero)
    std::cout << "t=" << t << "\n";

    // 5) Унарный минус
    Int128 neg = -c;
    std::cout << "-c=" << neg << "\n";

    // 6) Сравнения (==, !=)
    Int128 p("123");
    Int128 q(123);
    std::cout << std::boolalpha;
    std::cout << "p==q: " << (p == q) << "\n";
    std::cout << "p!=q: " << (p != q) << "\n";

    // 7) Явные приведения
    Int128 small("9223372036854775807"); // INT64_MAX
    int64_t i64 = static_cast<int64_t>(small);
    double d = static_cast<double>(c);

    std::cout << "i64=" << i64 << "\n";
    std::cout << "d=" << d << "\n";

    // 8) Примеры со знаками/границами
    Int128 min128("-170141183460469231731687303715884105728"); // -2^127
    Int128 max128("170141183460469231731687303715884105727");  //  2^127 - 1
    std::cout << "min128=" << min128 << "\n";
    std::cout << "max128=" << max128 << "\n";

    return 0;
}
