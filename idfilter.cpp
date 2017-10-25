// Copyright (c) 2017, ChienYu Lin
// All rights reserved.
#include <iostream>
#include <string>
#include <vector>
#include <memory>
#include <string>
#include <fstream>
#include <streambuf>

class pattern_interface {
public:
    using pos_type = std::string::const_iterator;
    struct match_result {
        bool matched;
        pos_type pos;
    };
    virtual match_result match(pos_type begin, pos_type end) = 0;
    virtual ~pattern_interface() {}
};

class n_pattern : public pattern_interface {
public:
    using pattern_type = std::shared_ptr<pattern_interface>;
    virtual match_result match(pos_type begin, pos_type end) override {
        match_result m;
        m.pos = begin;
        while (m.pos != end) {
            m = m_pattern->match(m.pos, end);
            if (!m.matched) {
                break;
            }
        }
        return m;
    }
    virtual ~n_pattern() {}
    n_pattern(pattern_type pattern) : m_pattern(std::move(pattern)) {}
private:
    pattern_type m_pattern;
};

class and_pattern : public pattern_interface {
public:
    using patterns_type = std::vector<std::shared_ptr<pattern_interface>>;
    virtual match_result match(pos_type begin, pos_type end) override {
        match_result m;
        m.pos = begin;
        m.matched = true;
        for (auto p : m_patterns) {
            m = p->match(m.pos, end);
            if (!m.matched) {
                m.pos = begin;
                break;
            }
        }
        return m;
    }
    virtual ~and_pattern() {}
    and_pattern(patterns_type patterns) : m_patterns(std::move(patterns)) {}
private:
    patterns_type m_patterns;
};

class or_pattern : public pattern_interface {
public:
    using patterns_type = std::vector<std::shared_ptr<pattern_interface>>;
    virtual match_result match(pos_type begin, pos_type end) override {
        match_result m;
        m.pos = begin;
        m.matched = false;
        for (auto p : m_patterns) {
            m = p->match(begin, end);
            if (m.matched)
                break;
        }
        return m;
    }
    virtual ~or_pattern() {}
    or_pattern(patterns_type patterns) : m_patterns(std::move(patterns)) {}
private:
    patterns_type m_patterns;
};
template <typename FnT, typename OnMatchedFnT>
class fn_pattern;

template <typename FnT, typename OnMatchedFnT = void>
class fn_pattern : public pattern_interface {
public:
    virtual match_result match(pos_type begin, pos_type end) override {
        auto m = m_fn(begin,end);
        if (m.matched) {
            std::string s;
            std::copy(begin, m.pos, std::back_insert_iterator<std::string>(s));
            m_on_matched_fn(std::move(s));
        }
        return m;
    }
    virtual ~fn_pattern() {}
    fn_pattern(FnT fn, OnMatchedFnT on_matched_fn)
        : m_fn(std::move(fn))
        , m_on_matched_fn(on_matched_fn)
        {}
private:
    FnT m_fn;
    OnMatchedFnT m_on_matched_fn;
};

template <typename FnT>
class fn_pattern<FnT,void> : public pattern_interface {
public:
    virtual match_result match(pos_type begin, pos_type end) override {
        return m_fn(begin,end);
    }
    virtual ~fn_pattern() {}
    fn_pattern(FnT fn) : m_fn(std::move(fn)) {}
private:
    FnT m_fn;
};

template <typename FnT>
decltype(auto) make_pattern(FnT fn) {
    return fn_pattern<FnT>(std::move(fn));
}

template <typename FnT, typename OnMatchedFnT>
decltype(auto) make_pattern(FnT fn, OnMatchedFnT on_matched_fn) {
    return fn_pattern<FnT,OnMatchedFnT>(std::move(fn), std::move(on_matched_fn));
}

template <typename FnT>
decltype(auto) make_shared_pattern(FnT fn) {
    return std::make_shared<fn_pattern<FnT>>(std::move(fn));
}

template <typename FnT, typename OnMatchedFnT>
decltype(auto) make_shared_pattern(FnT fn, OnMatchedFnT on_matched_fn) {
    return std::make_shared<fn_pattern<FnT,OnMatchedFnT>>(std::move(fn), std::move(on_matched_fn));
}


pattern_interface::match_result
parse_block_comment(
    std::string::const_iterator b /*begin*/,
    std::string::const_iterator e /*end*/) {
    pattern_interface::match_result m {.matched = false, .pos = b};
    int state = 0;
    auto i = b;
    for (; i != e; ++i) {
        switch (state) {
        case 0:
            if (*i == '/') {
                ++state;
                continue;
            }
            break;
        case 1:
            if (*i == '*'){
                ++state;
                continue;
            }
            break;
        case 2:
            if (*i == '*'){
                ++state;
            }
            continue;
        case 3:
            if (*i == '/') {
                m.pos = ++i;
                m.matched = true;
            }
        }
        break;
    }
    return m;
}

pattern_interface::match_result
parse_line_comment(
    std::string::const_iterator b /*begin*/,
    std::string::const_iterator e /*end*/) {
    pattern_interface::match_result m0 {false, b};
    pattern_interface::match_result m {false, b};
    int state = 0;
    auto i = b;
    for (; i != e; ++i) {
        switch (state) {
        case 0:
            if (*i == '/') {
                ++state;
                continue;
            }
            break;
        case 1:
            if (*i == '/'){
                ++state;
                continue;
            }
            break;
        case 2:
            if (*i != '\n'){
                continue;
            }
            ++i;
            break;
        }
        break;
    }
    if (state == 2) {
        m.matched = true;
        m.pos = i;
    }
    return m;
}

pattern_interface::match_result
parse_string_literal(
    std::string::const_iterator b /*begin*/,
    std::string::const_iterator e /*end*/) {
    pattern_interface::match_result m {false, b};
    int state = 0;
    auto i = b;
    enum {
        FAILED    = -1,
        BEGIN     =  0,
        NORMAL    =  2,
        ESCAPED   =  3,
        COMPLETED =  4,
    };
    for (; i != e; ++i) {
        switch (state) {
        case BEGIN:
            if (*i == '\"') {
                state = NORMAL;
                continue;
            } else {
                m.matched = false;
                state = FAILED;
                continue;
            }
        case NORMAL:
            if (*i == '\\') {
                return m;
                state = ESCAPED;
                continue;
            } else if (*i == '\"') {
                m.matched = true;
                state = COMPLETED;
                continue;
            } else {
                // state = NORMAL;
                continue;
            }
        case ESCAPED:
            state = NORMAL;
            continue;
        case COMPLETED:
        case FAILED:
            break;
        }
        break;
    }
    m.pos = i;
    return m;
}

pattern_interface::match_result
parse_char_literal(
    std::string::const_iterator b /*begin*/,
    std::string::const_iterator e /*end*/) {
    pattern_interface::match_result m {false, b};
    int state = 0;
    auto i = b;
    enum {
        FAILED    = -1,
        BEGIN     =  0,
        NORMAL    =  2,
        ESCAPED   =  3,
        COMPLETED =  4,
    };
    for (; i != e; ++i) {
        switch (state) {
        case BEGIN:
            if (*i == '\'') {
                state = NORMAL;
                continue;
            } else {
                m.matched = false;
                state = FAILED;
                continue;
            }
        case NORMAL:
            if (*i == '\\') {
                return m;
                state = ESCAPED;
                continue;
            } else if (*i == '\'') {
                m.matched = true;
                state = COMPLETED;
                continue;
            } else {
                // state = NORMAL;
                continue;
            }
        case ESCAPED:
            state = NORMAL;
            continue;
        case COMPLETED:
        case FAILED:
            break;
        }
        break;
    }
    m.pos = i;
    return m;
}

pattern_interface::match_result
parse_identifier(
    std::string::const_iterator b /*begin*/,
    std::string::const_iterator e /*end*/) {
    pattern_interface::match_result m {false, b};
    int state = 0;
    auto i = b;
    enum {
        FAILED    = -1,
        BEGIN     =  0,
        REST      =  1,
        COMPLETED =  2,
    };
    for (; i != e; ++i) {
        switch (state) {
        case BEGIN:
            if ((*i >= 'a' && *i <= 'z') ||
                (*i >= 'A' && *i <= 'Z') ||
                *i == '_') {
                m.matched = true;
                state = REST;
                continue;
            } else {
                m.matched = false;
                // state = FAILED;
                return m;
            }
        case REST:
            if ((*i >= 'a' && *i <= 'z') ||
                (*i >= 'A' && *i <= 'Z') ||
                (*i >= '0' && *i <= '9') ||
                *i == '_') {
                state = REST;
                continue;
            }
            break;
        }
        break;
    }
    if (state == REST) {
        m.pos = i;
        return m;
    }
    return m;
}

pattern_interface::match_result
parse_a_char(
    std::string::const_iterator b /*begin*/,
    std::string::const_iterator e /*end*/) {
    if (b != e) {
        return {true, ++b};
    }
    return {false, e};
}

pattern_interface::match_result
parse_epsilon(
    std::string::const_iterator b /*begin*/,
    std::string::const_iterator e /*end*/) {
    return {true, b};
}

int main(int argc, char** argv) {
    std::ifstream t;
    std::istream * is;
    std::string str;
    if (argc < 2) {
        is = &std::cin;
    } else {
        t.open(argv[1]);
        if (!t.is_open())
            return 0;
        t.seekg(0, std::ios::end);
        str.reserve(t.tellg());
        t.seekg(0, std::ios::beg);
        is = &t;
    }
    str.assign(std::istreambuf_iterator<char>(*is), std::istreambuf_iterator<char>());
    auto p1 = std::static_pointer_cast<pattern_interface>(make_shared_pattern(parse_block_comment));
    auto p2 = std::static_pointer_cast<pattern_interface>(make_shared_pattern(parse_line_comment));
    auto p3 = std::static_pointer_cast<pattern_interface>(make_shared_pattern(parse_string_literal));
    auto p4 = std::static_pointer_cast<pattern_interface>(make_shared_pattern(parse_char_literal));
    auto p5 = std::static_pointer_cast<pattern_interface>(make_shared_pattern(parse_identifier, [](auto s) { std::cout << s << std::endl;}));
    auto p6 = std::static_pointer_cast<pattern_interface>(make_shared_pattern(parse_a_char));
    auto p7 = std::static_pointer_cast<pattern_interface>(make_shared_pattern(parse_epsilon));
    std::vector<std::shared_ptr<pattern_interface>> ps = {p1, p2, p3, p4, p5, p6, p7};
    auto po = std::make_shared<or_pattern>(ps);
    auto pon = std::make_shared<n_pattern>(po);
    auto p = pon;
    // [NOTE]
    // May match patterns directly on stream,
    // without waiting the imput steam to be
    // completed to proceed further matching.
    auto m = p->match(str.cbegin(), str.cend());
    return 0;
}
