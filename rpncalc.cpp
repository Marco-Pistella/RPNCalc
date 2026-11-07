// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Marco Pistella
/*
 * RPNCalc - RPN (Reverse Polish Notation) calculator, Far Manager 3.x plug-in (32 and 64 bit)
 * ===========================================================================================================
 * A calculator with a stack, used in two ways:
 *   - a dialog, opened from the plug-ins menu (F11, "RPN Calculator"): the stack, an input line and a status line;
 *   - the Far command line with the prefix "rpn:" (for example   rpn: 3 4 + 5 *  ): the result is shown in a
 *     message box.
 * The stack and the angle mode are shared by the dialog and the command line and are kept in the Far settings.
 *
 * Structure of the file:
 *   1. GUIDs, identity, help topics, and the replacements for the libstdc++ error helpers (size of the DLL).
 *   2. Messages: the translatable texts (ids, built-in English texts, GetMsg, FormatMsg).
 *   3. The engine: number parser and formatter, and the Engine class (stack, angle mode, undo history, commands).
 *   4. Global state and persistence (Far settings).
 *   5. The dialog and the "rpn:" command line.
 *   6. The exported functions.
 *
 * Design notes:
 *   - A line is atomic: Engine::Eval restores the stack, the angle mode and the undo history when a command fails.
 *   - Numbers are read and written with the C runtime (strtod, snprintf), not with iostream or locale classes, so
 *     the result does not depend on the Windows locale. The same syntax is used for typing, for the saved stack and
 *     for the text inserted in the Far command line.
 *   - No C++ exception may leave a function that Far calls (DlgProc, OpenW): with the reduced terminate handler it
 *     would abort Far. A failure such as lack of memory is shown in the status line or in a message box.
 *   - Every text shown comes from a Far language file (.lng) in the folder of the DLL; the English texts are also
 *     built in (see "Messages"). The help (F1) is the file RPNCalcEng.hlf, in the same folder.
 *
 * Build: see build.bat (MinGW-w64, plugin.hpp of Far 3). A 32 bit DLL must be linked with -Wl,--kill-at so that the
 * stdcall exports have the undecorated names that Far looks for.
 */

#define NOMINMAX
#include <windows.h>

// <iostream>, <sstream>, <locale> and <unordered_map> are not used on purpose: in a statically linked DLL they pull
// in the libstdc++ locale code and make the binary several hundred KB larger. Numbers go through the C runtime;
// tokenizing and command lookup are hand-written.
#include <algorithm>
#include <clocale>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cwctype>
#include <memory>
#include <new>
#include <string>
#include <vector>

#include "plugin.hpp"

#define EXPORT extern "C" __declspec(dllexport)

// ==================== GUIDs, identity and help topics ====================
static const GUID PluginGuid = { 0x7b2d6c41, 0x9a3e, 0x4f58, { 0x8e, 0x1b, 0x5c, 0x0d, 0x2a, 0x94, 0xf7, 0xe3 } };
static const GUID MenuGuid   = { 0x1e5f8a93, 0x3c27, 0x4b60, { 0x9d, 0x14, 0xa6, 0xe8, 0xb0, 0xc7, 0x2f, 0x51 } };
static const GUID DialogGuid = { 0xc4a91d07, 0x62eb, 0x4e3f, { 0xb7, 0x85, 0x0f, 0x3d, 0x91, 0xa8, 0xe2, 0x64 } };
static const GUID MsgGuid    = { 0x5d1b3e8a, 0x7f92, 0x4c06, { 0xa3, 0xd5, 0x2b, 0x84, 0xe9, 0xf1, 0xc6, 0x70 } };

// Name, description, author and version shown by Far (GetGlobalInfoW). The version must be the same as the one in
// rpncalc.rc, which sets the properties of the DLL in Windows Explorer.
#define PLUGIN_NAME       L"RPNCalc"
#define PLUGIN_DESC       L"RPN (Reverse Polish Notation) calculator"
#define PLUGIN_AUTHOR     L"Marco Pistella"
#define PLUGIN_VER_MAJOR  1
#define PLUGIN_VER_MINOR  0
#define PLUGIN_VER_PATCH  0
#define PLUGIN_VER_BUILD  0

// Help topic names defined in RPNCalcEng.hlf
static const wchar_t* const HELP_MAIN    = L"Contents";
static const wchar_t* const HELP_CMDLINE = L"CommandLine";

static PluginStartupInfo PSI;

#if defined(__GLIBCXX__)
// Replaces libstdc++'s default terminate handler, which formats the exception type through the C++ demangler.
// Defining it here keeps the library's version, and the demangler, out of the link.
namespace __gnu_cxx { void __verbose_terminate_handler() { std::abort(); } }

#ifndef RPN_KEEP_FUNCTEXCEPT
// Replaces libstdc++'s <functexcept.h> helpers, which std::vector and std::wstring call for length and range
// errors. The library versions build std::logic_error & co. around a std::string, which pulls the narrow-string
// code, the random device code and about 70 KB into the DLL. The conditions they report (a size beyond max_size(),
// a position outside the string) cannot be produced by this code; if one ever were, std::bad_alloc is thrown, which
// every boundary of the plug-in already handles ("Out of memory"). Define RPN_KEEP_FUNCTEXCEPT to keep the library
// versions instead, if this block conflicts with the libstdc++ of the compiler in use.
namespace std {
void __throw_bad_alloc()                         { throw std::bad_alloc(); }
void __throw_bad_array_new_length()              { throw std::bad_alloc(); }
void __throw_length_error(const char*)           { throw std::bad_alloc(); }
void __throw_logic_error(const char*)            { throw std::bad_alloc(); }
void __throw_out_of_range(const char*)           { throw std::bad_alloc(); }
void __throw_out_of_range_fmt(const char*, ...)  { throw std::bad_alloc(); }
}
#endif
#endif

// ==================== messages ====================
// Every text shown to the user comes from the language file RPNCalc<Language>.lng in the folder of the DLL: Far
// picks the file whose ".Language" is its interface language, or the English one. The id of a message is the
// position of its string in that file, counted from 0, so this enum, kDefaultMsg and RPNCalcEng.lng must list the
// messages in the same order. "%s" in a text stands for a value (see FormatMsg). The English texts are also built
// in (kDefaultMsg, identical to RPNCalcEng.lng) and are used when Far returns nothing, that is when there is no
// language file.
// To add a message: add its id before M_COUNT, its text at the same position in kDefaultMsg and in every .lng file.
enum {
    M_TITLE,                // dialog title, plug-in menu item, title of the message boxes
    M_HINT1,                // key hints of the dialog, first line
    M_HINT2,                // key hints of the dialog, second line
    M_ANGLES_RAD,           // status line: angle mode
    M_ANGLES_DEG,
    M_STACK_DEPTH,          // status line and message box; %s = number of entries
    M_ERROR,                // status line when a line has an error; %s = the error text
    M_RESULT,               // message box of "rpn:"; %s = X
    M_STACK_EMPTY,

    M_ERR_NOT_ENOUGH,       // errors of a line (RpnError)
    M_ERR_DIV_ZERO,
    M_ERR_INVALID_RESULT,
    M_ERR_STACK_FULL,
    M_ERR_NO_UNDO,
    M_ERR_UNKNOWN_CMD,      // %s = the token
    M_ERR_BAD_NUMBER,       // %s = the token
    M_ERR_BAD_DIGIT,        // %s = the token
    M_ERR_TOO_LARGE,        // %s = the token
    M_ERR_NOT_INTEGER,
    M_ERR_INT_TOO_LARGE,
    M_ERR_SQRT_NEG,
    M_ERR_LOG_NONPOS,
    M_ERR_ARG_RANGE,
    M_ERR_FACTORIAL,

    M_ERR_MEMORY,           // failures that are not an error of the user
    M_ERR_INTERNAL,
    M_ERR_FATAL,            // message box shown when OpenW itself fails

    M_COUNT
};

static const wchar_t* const kDefaultMsg[] = {
    L"RPN Calculator",                                               // M_TITLE
    L"Enter=calc  Ctrl+Enter=cmd line  BS=drop  Del=clr  F1=Help",   // M_HINT1
    L"Ctrl+BS=undo  Alt+F1=-  Alt+F2=chs  Alt+F3=inv  Alt+F4=swap",  // M_HINT2
    L"Angles: radians",                                              // M_ANGLES_RAD
    L"Angles: degrees",                                              // M_ANGLES_DEG
    L"Stack depth: %s",                                              // M_STACK_DEPTH
    L"Error: %s",                                                    // M_ERROR
    L"X = %s",                                                       // M_RESULT
    L"Stack is empty",                                               // M_STACK_EMPTY
    L"Not enough operands",                                          // M_ERR_NOT_ENOUGH
    L"Division by zero",                                             // M_ERR_DIV_ZERO
    L"Invalid result",                                               // M_ERR_INVALID_RESULT
    L"Stack is full",                                                // M_ERR_STACK_FULL
    L"Nothing to undo",                                              // M_ERR_NO_UNDO
    L"Unknown command: %s",                                          // M_ERR_UNKNOWN_CMD
    L"Invalid number: %s",                                           // M_ERR_BAD_NUMBER
    L"Invalid digit for the base: %s",                               // M_ERR_BAD_DIGIT
    L"Number too large (max 2^53): %s",                              // M_ERR_TOO_LARGE
    L"Not an integer",                                               // M_ERR_NOT_INTEGER
    L"Integer too large (max 2^53)",                                 // M_ERR_INT_TOO_LARGE
    L"Square root of a negative number",                             // M_ERR_SQRT_NEG
    L"Logarithm of a non-positive number",                           // M_ERR_LOG_NONPOS
    L"Argument out of range (-1..1)",                                // M_ERR_ARG_RANGE
    L"Factorial needs an integer 0-170",                             // M_ERR_FACTORIAL
    L"Out of memory",                                                // M_ERR_MEMORY
    L"Internal error",                                               // M_ERR_INTERNAL
    L"Out of memory or internal error",                              // M_ERR_FATAL
};
static_assert(sizeof kDefaultMsg / sizeof kDefaultMsg[0] == M_COUNT, "one built-in text per message id");

// Text of the message with the given id: the one from the language file, or the built-in English text
// when Far returns none (Far returns an empty string if it finds no language file).
static const wchar_t* GetMsg(int id)
{
    const wchar_t* s = PSI.GetMsg ? PSI.GetMsg(&PluginGuid, id) : nullptr;
    return (s && *s) ? s : kDefaultMsg[id];
}

// Text of the message with the given id, with its first "%s" replaced by arg. A text without "%s" is
// returned unchanged.
static std::wstring FormatMsg(int id, const std::wstring& arg)
{
    std::wstring s = GetMsg(id);
    const size_t p = s.find(L"%s");
    if (p != std::wstring::npos) s.replace(p, 2, arg);
    return s;
}

// ==================== RPN engine ====================
// Thrown by the engine for any user-level error; msg is shown to the user.
struct RpnError { std::wstring msg; };

// Equivalent of "%.<prec>g" in the "C" locale: '.' as decimal separator and an exponent of at
// least two digits, regardless of the runtime's current locale or of its exponent width.
static std::wstring FormatNum(double v, int prec = 15)
{
    char buf[64];
    std::snprintf(buf, sizeof buf, "%.*g", prec, v);

    const char* dp = std::localeconv()->decimal_point;     // decimal separator used by the C runtime
    std::wstring w;
    for (size_t i = 0; buf[i]; i++) {
        if (dp[0] && !dp[1] && buf[i] == dp[0]) { w += L'.'; continue; }
        w += (wchar_t)(unsigned char)buf[i];
        if (buf[i] == 'e' && (buf[i + 1] == '+' || buf[i + 1] == '-')) {
            w += (wchar_t)buf[++i];                          // exponent sign
            size_t digits = std::strlen(buf + i + 1);        // some runtimes print three exponent digits (e+015)
            while (digits > 2 && buf[i + 1] == '0') { i++; digits--; }   // strip the leading zeros beyond two digits
        }
    }
    return w;
}

// A stack entry: the value and the base it is displayed in.
// base: 0 = normal display, 2 = n#B, 10 = plain integer (never an exponent), 16 = n#H.
struct Item
{
    double v;
    int    base;
};

static const double kMaxInt = 9007199254740992.0;   // 2^53: largest range in which a double holds every integer exactly
static const size_t kMaxStack = 10000;              // maximum stack depth; must stay below 2^14 (see Engine::Total)

// Formats an integer-valued double in base 2, 10 or 16, with the #B / #H suffix for bases 2 and 16.
static std::wstring FormatBase(double v, int base)
{
    const unsigned long long b = (unsigned long long)base;
    unsigned long long m = (unsigned long long)std::fabs(v);
    std::wstring s;
    do {
        s.push_back(L"0123456789ABCDEF"[m % b]);
        m /= b;
    } while (m);
    if (std::signbit(v)) s.push_back(L'-');              // also taken for -0, matching the normal form
    std::reverse(s.begin(), s.end());
    if (base == 2)  s += L"#B";
    if (base == 16) s += L"#H";
    return s;
}

// Display text of an entry; prec is the number of significant digits of the normal form.
static std::wstring FormatItem(const Item& it, int prec = 15)
{
    return it.base ? FormatBase(it.v, it.base) : FormatNum(it.v, prec);
}

// Text stored in the settings. Base 10 gets an explicit #D suffix so that it is not read back as
// a normal-form number; every other form is its display form, with 17 digits so that it round-trips.
static std::wstring FormatSave(const Item& it)
{
    return it.base == 10 ? FormatBase(it.v, 10) + L"#D" : FormatItem(it, 17);
}

// Shortest "%.15g".."%.17g" text that parses back to exactly the same double. Used when X is handed
// to another consumer (the Far command line) and must not lose precision.
static std::wstring FormatExact(const Item& it);   // defined after ParseNum

// True if the token starts like a decimal number: optional sign, then a digit, or '.'/',' followed by a digit.
static bool LooksNumeric(const std::wstring& t)
{
    size_t i = 0;
    if (t.empty()) return false;
    if (t[i] == L'+' || t[i] == L'-') i++;
    if (i >= t.size()) return false;
    if (t[i] >= L'0' && t[i] <= L'9') return true;
    return (t[i] == L'.' || t[i] == L',') && i + 1 < t.size() && t[i + 1] >= L'0' && t[i + 1] <= L'9';
}

// Parses a decimal number: [+-] digits [. [digits]] or [+-] . digits, then an optional exponent
// (e|E [+-] digits). ',' is accepted as the decimal separator. Hexadecimal floats, inf and nan are
// rejected, as is any trailing garbage. The text is rebuilt in a narrow buffer using the C runtime's
// own decimal separator and converted with strtod. Throws RpnError if the text is not a finite number.
static double ParseNum(const std::wstring& s)
{
    char buf[512];
    if (s.size() >= sizeof buf) throw RpnError{ FormatMsg(M_ERR_BAD_NUMBER, s) };

    const char* dp = std::localeconv()->decimal_point;     // strtod expects the runtime's decimal separator
    const char point = (dp[0] && !dp[1]) ? dp[0] : '.';

    size_t i = 0, n = 0, digits = 0;
    const size_t len = s.size();
    if (i < len && (s[i] == L'+' || s[i] == L'-')) buf[n++] = (char)s[i++];
    while (i < len && s[i] >= L'0' && s[i] <= L'9') { buf[n++] = (char)s[i++]; digits++; }
    if (i < len && (s[i] == L'.' || s[i] == L',')) {
        buf[n++] = point; i++;
        while (i < len && s[i] >= L'0' && s[i] <= L'9') { buf[n++] = (char)s[i++]; digits++; }
    }
    bool ok = digits > 0;
    if (ok && i < len && (s[i] == L'e' || s[i] == L'E')) {
        buf[n++] = 'e'; i++;
        if (i < len && (s[i] == L'+' || s[i] == L'-')) buf[n++] = (char)s[i++];
        size_t expDigits = 0;
        while (i < len && s[i] >= L'0' && s[i] <= L'9') { buf[n++] = (char)s[i++]; expDigits++; }
        ok = expDigits > 0;
    }
    double v = 0;
    if (ok && i == len) {
        buf[n] = 0;
        char* end = nullptr;
        v = std::strtod(buf, &end);
        if (*end == 0 && std::isfinite(v)) return v;   // overflow gives inf and is rejected here
    }
    throw RpnError{ FormatMsg(M_ERR_BAD_NUMBER, s) };
}

static std::wstring FormatExact(const Item& it)
{
    if (it.base) return FormatItem(it);
    for (int prec = 15; prec < 17; prec++) {
        const std::wstring s = FormatNum(it.v, prec);
        try { if (ParseNum(s) == it.v) return s; } catch (const RpnError&) {}
    }
    return FormatNum(it.v, 17);
}

static bool IsBlank(wchar_t c) { return c == L' ' || (c >= 9 && c <= 13); }

// Splits a line into tokens separated by runs of white space.
static std::vector<std::wstring> Split(const std::wstring& s)
{
    std::vector<std::wstring> out;
    size_t i = 0;
    while (i < s.size()) {
        while (i < s.size() && IsBlank(s[i])) i++;
        size_t j = i;
        while (j < s.size() && !IsBlank(s[j])) j++;
        if (j > i) out.push_back(s.substr(i, j - i));
        i = j;
    }
    return out;
}

// Command name -> numeric id.
struct Cmd { const wchar_t* name; int id; };

// Linear lookup in a command table; returns the id, or -1 if the name is not present.
static int Lookup(const Cmd* tab, size_t n, const std::wstring& t)
{
    for (size_t i = 0; i < n; i++)
        if (t == tab[i].name) return tab[i].id;
    return -1;
}

// Parses an integer with a base suffix: 1010#B (binary), 1F#H (hexadecimal) or 255#D (decimal).
// An optional leading sign is allowed; digits and suffix are case insensitive. The magnitude is
// limited to 2^53. Throws RpnError on a malformed token, a digit outside the base or an overflow.
static Item ParseBased(const std::wstring& s)
{
    const size_t p = s.rfind(L'#');
    if (p == 0 || p == std::wstring::npos || s.size() != p + 2)
        throw RpnError{ FormatMsg(M_ERR_BAD_NUMBER, s) };

    const wchar_t sfx = (wchar_t)towupper(s[p + 1]);
    const int base = sfx == L'B' ? 2 : sfx == L'H' ? 16 : sfx == L'D' ? 10 : 0;
    if (!base) throw RpnError{ FormatMsg(M_ERR_BAD_NUMBER, s) };

    size_t i = 0;
    bool neg = false;
    if (s[0] == L'+' || s[0] == L'-') { neg = (s[0] == L'-'); i = 1; }
    if (i >= p) throw RpnError{ FormatMsg(M_ERR_BAD_NUMBER, s) };

    double v = 0;
    for (; i < p; i++) {
        const wchar_t c = s[i];
        int d;
        if (c >= L'0' && c <= L'9')      d = c - L'0';
        else if (c >= L'a' && c <= L'f') d = c - L'a' + 10;
        else if (c >= L'A' && c <= L'F') d = c - L'A' + 10;
        else throw RpnError{ FormatMsg(M_ERR_BAD_NUMBER, s) };
        if (d >= base) throw RpnError{ FormatMsg(M_ERR_BAD_DIGIT, s) };
        if (v > std::floor((kMaxInt - d) / base)) throw RpnError{ FormatMsg(M_ERR_TOO_LARGE, s) };   // v * base + d must stay <= 2^53
        v = v * base + d;
    }
    return Item{ neg ? -v : v, base };
}

// Snapshot restored by UNDO: the stack and the angle mode.
struct EngineState
{
    std::vector<Item> st;
    bool              deg;
};
typedef std::shared_ptr<const EngineState> StatePtr;

// Ids of the unary functions (see Engine::Unary).
enum UOp {
    U_NEG, U_ABS, U_INV, U_SQ, U_SQRT, U_CBRT, U_EXP, U_LN, U_LOG, U_LOG2,
    U_SIN, U_COS, U_TAN, U_ASIN, U_ACOS, U_ATAN, U_SINH, U_COSH, U_TANH,
    U_FACT, U_FLOOR, U_CEIL, U_ROUND, U_TRUNC
};

class Engine
{
public:
    std::vector<Item> st;     // the stack; the last element is X (the top)
    bool deg = false;         // angle mode: false = radians, true = degrees

    // Evaluates a line of white-space separated tokens. Returns false and sets err on failure;
    // in that case the stack, the angle mode and the undo history are rolled back to their
    // state before the call, so a line is applied atomically.
    bool Eval(const std::wstring& line, std::wstring& err)
    {
        const std::vector<Item> backup = st;
        const bool backupDeg = deg;
        const std::vector<StatePtr> backupHist = hist;   // snapshots are shared, so copying the vector is cheap
        try {
            for (const std::wstring& tok : Split(line)) Token(tok);
            return true;
        } catch (const RpnError& e) {
            st = backup;
            deg = backupDeg;
            hist = backupHist;
            err = e.msg;
            return false;
        }
    }

private:
    static constexpr double kPi = 3.14159265358979323846;
    static constexpr size_t kUndoDepth = 100;   // maximum number of undo steps kept

    std::vector<StatePtr> hist;                 // undo history, oldest first; not persisted

    void Need(size_t n) { if (st.size() < n) throw RpnError{ GetMsg(M_ERR_NOT_ENOUGH) }; }
    double Pop() { double v = st.back().v; st.pop_back(); return v; }

    // Pushes a value; rejects non-finite results and enforces the stack depth limit.
    void Push(double v, int base = 0)
    {
        if (!std::isfinite(v)) throw RpnError{ GetMsg(M_ERR_INVALID_RESULT) };
        if (st.size() >= kMaxStack) throw RpnError{ GetMsg(M_ERR_STACK_FULL) };
        st.push_back(Item{ v, base });
    }

    // Angle conversion for the current mode. ToRad multiplies by (kPi / 180) rather than
    // computing x * kPi / 180, which would overflow for |x| > 5.7e307.
    double ToRad(double x) const { return deg ? x * (kPi / 180.0) : x; }
    double FromRad(double x) const { return deg ? x * 180.0 / kPi : x; }

    // Noise suppression for sin/cos/tan. Near a zero of the function the computed result is
    // rounding noise of the argument (cos(pi/2) = 6.1e-17). If |r| is below four ULPs of the
    // argument a, the result carries no information and 0 is returned. Not applied for |a| >= 1e9,
    // where the argument's own resolution is too coarse for the test to mean anything. A small
    // result of a small argument (sin(1e-16)) is far above that threshold and is kept.
    static double Snap(double r, double a)
    {
        a = std::fabs(a);
        if (a < 1e9 && std::fabs(r) < 4 * (std::nextafter(a, HUGE_VAL) - a)) return 0.0;
        return r;
    }

    // Sum of the whole stack, or its average.
    // The plain sum is used when it is finite. If it overflows (1e308 1e308 -1e308), the entries are
    // summed scaled by 2^-14 and the result is scaled back. Since the stack holds at most
    // kMaxStack < 2^14 finite entries, no partial sum of the scaled values can overflow, so a
    // representable result is not lost to an intermediate overflow.
    // Accuracy is that of ordinary floating-point summation: heavy cancellation can still lose small
    // terms (1e308 1e-100 1e308 -1e308 -1e308 sum gives 0), and on the scaled path terms below about
    // 2^-1008 are not preserved exactly.
    double Total(bool average = false) const
    {
        const double n = (double)st.size();
        double s = 0;
        for (const Item& i : st) s += i.v;
        if (std::isfinite(s)) return average ? s / n : s;
        s = 0;
        for (const Item& i : st) s += std::ldexp(i.v, -14);
        return std::ldexp(average ? s / n : s, 14);
    }

    // Requires X to be an integer exactly representable as a double (|X| <= 2^53).
    void NeedInt()
    {
        Need(1);
        const double x = st.back().v;
        if (x != std::floor(x)) throw RpnError{ GetMsg(M_ERR_NOT_INTEGER) };
        if (std::fabs(x) > kMaxInt) throw RpnError{ GetMsg(M_ERR_INT_TOO_LARGE) };
    }

    // Element-wise equality of two stacks: value (including the sign of zero) and display base.
    static bool SameItems(const std::vector<Item>& a, const std::vector<Item>& b)
    {
        if (a.size() != b.size()) return false;
        for (size_t i = 0; i < a.size(); i++)
            if (a[i].v != b[i].v || std::signbit(a[i].v) != std::signbit(b[i].v) || a[i].base != b[i].base)
                return false;
        return true;
    }

    // Appends a snapshot to the undo history, dropping the oldest one beyond kUndoDepth.
    void Remember(const StatePtr& s)
    {
        hist.push_back(s);
        if (hist.size() > kUndoDepth) hist.erase(hist.begin());
    }

    // Restores the most recent snapshot and removes it from the history.
    void Undo()
    {
        if (hist.empty()) throw RpnError{ GetMsg(M_ERR_NO_UNDO) };
        st  = hist.back()->st;
        deg = hist.back()->deg;
        hist.pop_back();
    }

    // Executes one token. A snapshot is taken before the token runs and is recorded as an undo step
    // only if the token changed the stack or the angle mode. "undo" itself is never recorded.
    void Token(const std::wstring& raw)
    {
        std::wstring t = raw;
        for (auto& c : t) c = (wchar_t)towlower(c);

        if (t == L"undo") { Undo(); return; }

        const StatePtr before(std::make_shared<EngineState>(EngineState{ st, deg }));
        Execute(raw, t);
        if (deg != before->deg || !SameItems(st, before->st)) Remember(before);
    }

    // Dispatches a token: based integer, decimal number, then binary operator, unary function or
    // stack/mode command. raw keeps the original case for messages; t is the lower-cased form
    // used for command matching.
    void Execute(const std::wstring& raw, const std::wstring& t)
    {
        if (raw.find(L'#') != std::wstring::npos) {   // 1010#B, FF#H, 255#D
            const Item it = ParseBased(raw);
            Push(it.v, it.base);
            return;
        }
        if (LooksNumeric(raw)) { Push(ParseNum(raw)); return; }

        if (Binary(t) || Unary(t) || StackOp(t)) return;
        throw RpnError{ FormatMsg(M_ERR_UNKNOWN_CMD, raw) };
    }

    // Binary operators. Returns false if t is not one of them.
    bool Binary(const std::wstring& t)
    {
        static const Cmd tab[] = {
            { L"+", 1 }, { L"-", 2 }, { L"*", 3 }, { L"/", 4 }, { L"^", 5 }, { L"**", 5 },
            { L"%", 6 }, { L"mod", 6 }, { L"atan2", 7 }, { L"min", 8 }, { L"max", 9 }
        };
        const int op = Lookup(tab, sizeof tab / sizeof tab[0], t);
        if (op < 0) return false;

        Need(2);
        const double b = Pop(), a = Pop();   // b = X, a = Y
        double r = 0;
        switch (op) {
        case 1: r = a + b; break;
        case 2: r = a - b; break;
        case 3: r = a * b; break;
        case 4: if (b == 0) throw RpnError{ GetMsg(M_ERR_DIV_ZERO) }; r = a / b; break;
        case 5: r = std::pow(a, b); break;
        case 6: if (b == 0) throw RpnError{ GetMsg(M_ERR_DIV_ZERO) }; r = std::fmod(a, b); break;   // sign follows the dividend
        case 7: r = FromRad(std::atan2(a, b)); break;   // atan2(Y, X)
        case 8: r = std::min(a, b); break;
        case 9: r = std::max(a, b); break;
        }
        Push(r);
        return true;
    }

    // Unary functions. Returns false if t is not one of them. Domain errors are reported here;
    // overflow and undefined results are caught by Push (M_ERR_INVALID_RESULT).
    bool Unary(const std::wstring& t)
    {
        static const Cmd tab[] = {
            { L"neg", U_NEG }, { L"chs", U_NEG }, { L"abs", U_ABS }, { L"inv", U_INV },
            { L"sq", U_SQ }, { L"sqrt", U_SQRT }, { L"cbrt", U_CBRT }, { L"exp", U_EXP },
            { L"ln", U_LN }, { L"log", U_LOG }, { L"log2", U_LOG2 },
            { L"sin", U_SIN }, { L"cos", U_COS }, { L"tan", U_TAN },
            { L"asin", U_ASIN }, { L"acos", U_ACOS }, { L"atan", U_ATAN },
            { L"sinh", U_SINH }, { L"cosh", U_COSH }, { L"tanh", U_TANH },
            { L"fact", U_FACT }, { L"floor", U_FLOOR }, { L"ceil", U_CEIL },
            { L"round", U_ROUND }, { L"int", U_TRUNC }
        };
        const int op = Lookup(tab, sizeof tab / sizeof tab[0], t);
        if (op < 0) return false;

        Need(1);
        const double x = Pop();
        double r = 0;
        switch (op) {
        case U_NEG:   r = -x; break;
        case U_ABS:   r = std::fabs(x); break;
        case U_INV:   if (x == 0) throw RpnError{ GetMsg(M_ERR_DIV_ZERO) }; r = 1.0 / x; break;
        case U_SQ:    r = x * x; break;
        case U_SQRT:  if (x < 0) throw RpnError{ GetMsg(M_ERR_SQRT_NEG) }; r = std::sqrt(x); break;
        case U_CBRT:  r = std::cbrt(x); break;
        case U_EXP:   r = std::exp(x); break;
        case U_LN:    if (x <= 0) throw RpnError{ GetMsg(M_ERR_LOG_NONPOS) }; r = std::log(x); break;
        case U_LOG:   if (x <= 0) throw RpnError{ GetMsg(M_ERR_LOG_NONPOS) }; r = std::log10(x); break;
        case U_LOG2:  if (x <= 0) throw RpnError{ GetMsg(M_ERR_LOG_NONPOS) }; r = std::log2(x); break;
        case U_SIN:   { const double a = ToRad(x); r = Snap(std::sin(a), a); break; }
        case U_COS:   { const double a = ToRad(x); r = Snap(std::cos(a), a); break; }
        case U_TAN:   { const double a = ToRad(x); r = Snap(std::tan(a), a); break; }
        case U_ASIN:  if (std::fabs(x) > 1) throw RpnError{ GetMsg(M_ERR_ARG_RANGE) }; r = FromRad(std::asin(x)); break;
        case U_ACOS:  if (std::fabs(x) > 1) throw RpnError{ GetMsg(M_ERR_ARG_RANGE) }; r = FromRad(std::acos(x)); break;
        case U_ATAN:  r = FromRad(std::atan(x)); break;
        case U_SINH:  r = std::sinh(x); break;
        case U_COSH:  r = std::cosh(x); break;
        case U_TANH:  r = std::tanh(x); break;
        case U_FACT:  // 170! is the largest factorial that fits in a double; tgamma(x + 1) is rounded to the nearest integer
            if (x < 0 || x != std::floor(x) || x > 170) throw RpnError{ GetMsg(M_ERR_FACTORIAL) };
            r = std::round(std::tgamma(x + 1.0));
            break;
        case U_FLOOR: r = std::floor(x); break;
        case U_CEIL:  r = std::ceil(x); break;
        case U_ROUND: r = std::round(x); break;   // halves away from zero
        case U_TRUNC: r = std::trunc(x); break;
        }
        Push(r);
        return true;
    }

    // Stack manipulation, display base, constants and angle mode. Returns false if t is not one of them.
    bool StackOp(const std::wstring& t)
    {
        if (t == L"dup")  { Need(1); const Item x = st.back(); Push(x.v, x.base); }
        else if (t == L"drop") { Need(1); st.pop_back(); }
        else if (t == L"swap") { Need(2); std::swap(st[st.size() - 1], st[st.size() - 2]); }
        else if (t == L"over") { Need(2); const Item y = st[st.size() - 2]; Push(y.v, y.base); }
        else if (t == L"rot")  { Need(3); std::rotate(st.end() - 3, st.end() - 2, st.end()); }   // Z->X, X->Y, Y->Z
        else if (t == L"clr" || t == L"clear") { st.clear(); }
        else if (t == L"sum")  { Need(1); const double s = Total(); st.clear(); Push(s); }
        else if (t == L"avg")  { Need(1); const double s = Total(true); st.clear(); Push(s); }
        else if (t == L"bin")  { NeedInt(); st.back().base = 2; }
        else if (t == L"hex")  { NeedInt(); st.back().base = 16; }
        else if (t == L"dec")  { NeedInt(); st.back().base = 10; }
        else if (t == L"pi")   { Push(kPi); }
        else if (t == L"e")    { Push(2.718281828459045235360287); }
        else if (t == L"deg")  { deg = true; }
        else if (t == L"rad")  { deg = false; }
        else return false;
        return true;
    }
};

// ==================== global state ====================
static Engine       g_eng;                    // single engine shared by the dialog and the command line
static std::wstring g_err;                    // message of the last failed Run, empty on success
static bool         g_loaded = false;         // the settings have been read
static bool         g_insertToCmdLine = false;   // set by Ctrl+Enter: insert X into the command line after the dialog closes

// Evaluates a line on the global engine; the error text, if any, is left in g_err.
static bool Run(const std::wstring& line)
{
    g_err.clear();
    return g_eng.Eval(line, g_err);
}

// ==================== persistence ====================
// Opens the plugin's settings storage; returns nullptr on failure. The handle is released with SCTL_FREE.
static HANDLE SettingsOpen()
{
    FarSettingsCreate c;
    std::memset(&c, 0, sizeof(c));
    c.StructSize = sizeof(c);
    c.Guid = PluginGuid;
    c.Handle = INVALID_HANDLE_VALUE;
    if (PSI.SettingsControl(INVALID_HANDLE_VALUE, SCTL_CREATE, 0, &c)) return c.Handle;
    return nullptr;
}

// Builds a zero-initialized FarSettingsItem addressing a value in the settings root.
static FarSettingsItem MakeSetting(const wchar_t* name, FARSETTINGSTYPES type)
{
    FarSettingsItem it;
    std::memset(&it, 0, sizeof(it));
    it.StructSize = sizeof(it);
    it.Root = 0;
    it.Name = name;
    it.Type = type;
    return it;
}

// Loads the stack and the angle mode once per session. The stack is stored as a string of
// space-separated tokens in the same syntax the parser accepts; unreadable tokens are skipped.
static void LoadState()
{
    if (g_loaded) return;
    g_loaded = true;

    HANDLE h = SettingsOpen();
    if (!h) return;

    FarSettingsItem s = MakeSetting(L"Stack", FST_STRING);
    if (PSI.SettingsControl(h, SCTL_GET, 0, &s) && s.String) {
        for (const std::wstring& tok : Split(s.String)) {
            if (g_eng.st.size() >= kMaxStack) break;
            try { g_eng.st.push_back(tok.find(L'#') != std::wstring::npos ? ParseBased(tok) : Item{ ParseNum(tok), 0 }); } catch (const RpnError&) {}
        }
    }
    FarSettingsItem d = MakeSetting(L"Degrees", FST_QWORD);
    if (PSI.SettingsControl(h, SCTL_GET, 0, &d)) g_eng.deg = d.Number != 0;

    PSI.SettingsControl(h, SCTL_FREE, 0, nullptr);
}

// Writes the stack (bottom first, in FormatSave form) and the angle mode to the settings.
// The undo history is not persisted.
static void SaveState()
{
    HANDLE h = SettingsOpen();
    if (!h) return;

    std::wstring text;
    for (const Item& it : g_eng.st) { text += FormatSave(it); text += L' '; }

    FarSettingsItem s = MakeSetting(L"Stack", FST_STRING);
    s.String = text.c_str();
    PSI.SettingsControl(h, SCTL_SET, 0, &s);

    FarSettingsItem d = MakeSetting(L"Degrees", FST_QWORD);
    d.Number = g_eng.deg ? 1 : 0;
    PSI.SettingsControl(h, SCTL_SET, 0, &d);

    PSI.SettingsControl(h, SCTL_FREE, 0, nullptr);
}

// ==================== dialog ====================
// Dialog geometry and control ids. The controls are laid out top to bottom: frame, LEVELS stack
// lines (highest level first), separator, input line, status line, two hint lines.
enum {
    LEVELS    = 8,                  // stack levels shown
    DW        = 72,                 // dialog width
    DH        = 17,                 // dialog height
    IW        = DW - 10,            // width of the text area inside the frame

    ID_BOX    = 0,
    ID_STACK0 = 1,                  // first of the LEVELS stack lines (ids 1..8)
    ID_SEP    = ID_STACK0 + LEVELS, // separator (9)
    ID_EDIT,                        // input line (10)
    ID_STATUS,                      // angle mode / depth or error text (11)
    ID_HINT,                        // key hints (12)
    ID_HINT2,                       // key hints, second line (13)
    ID_COUNT
};

// Decimal text of an unsigned number, without the C runtime.
static std::wstring Utow(size_t n)
{
    wchar_t b[24];
    int i = 24;
    do { b[--i] = (wchar_t)(L'0' + n % 10); n /= 10; } while (n);
    return std::wstring(b + i, b + 24);
}

// Removes leading and trailing spaces and tabs.
static std::wstring Trim(const std::wstring& s)
{
    size_t a = s.find_first_not_of(L" \t");
    if (a == std::wstring::npos) return L"";
    size_t b = s.find_last_not_of(L" \t");
    return s.substr(a, b - a + 1);
}

static void SetText(HANDLE h, int id, const std::wstring& s)
{
    PSI.SendDlgMessage(h, DM_SETTEXTPTR, id, (void*)s.c_str());
}

// Current text of the input line. The pointer returned by Far is only valid until the next dialog call,
// so it is copied.
static std::wstring GetEdit(HANDLE h)
{
    const wchar_t* p = (const wchar_t*)PSI.SendDlgMessage(h, DM_GETCONSTTEXTPTR, ID_EDIT, nullptr);
    return p ? p : L"";
}

// Text of one stack line: "<level>:" left-aligned and the value right-aligned. Level 1 is X.
// An empty level shows only its label.
static std::wstring StackLine(int level)
{
    std::wstring s = Utow((size_t)level) + L":";
    if ((int)g_eng.st.size() >= level) {
        std::wstring v = FormatItem(g_eng.st[g_eng.st.size() - level]);
        int pad = IW - (int)s.size() - (int)v.size();
        if (pad < 1) pad = 1;
        s.append(pad, L' ');
        s += v;
    }
    return s;
}

// Redraws the stack lines and the status line; redraw is suspended while the texts are updated.
// The status line shows g_err if set, otherwise the angle mode and the stack depth.
static void Refresh(HANDLE h)
{
    PSI.SendDlgMessage(h, DM_ENABLEREDRAW, FALSE, nullptr);
    for (int i = 0; i < LEVELS; i++)
        SetText(h, ID_STACK0 + i, StackLine(LEVELS - i));   // control order is top-down, so level 1 (X) ends up at the bottom
    if (!g_err.empty())
        SetText(h, ID_STATUS, FormatMsg(M_ERROR, g_err));
    else
        SetText(h, ID_STATUS, std::wstring(GetMsg(g_eng.deg ? M_ANGLES_DEG : M_ANGLES_RAD)) + L"   "
                              + FormatMsg(M_STACK_DEPTH, Utow(g_eng.st.size())));
    PSI.SendDlgMessage(h, DM_ENABLEREDRAW, TRUE, nullptr);
}

// Dialog procedure. Key events for the input line are intercepted in DN_CONTROLINPUT; returning TRUE consumes the
// key, otherwise it falls through to Far's default handling (editing, Esc, F1). The keys handled here:
//   Enter            evaluate the line (empty line: DUP)
//   Ctrl+Enter       evaluate, then close the dialog and insert X into the Far command line (empty line: no DUP)
//   + * / ^          empty line only: apply the operator at once
//   Backspace        empty line only: DROP
//   Delete           empty line only: CLR
//   Ctrl+Backspace   UNDO, also with text in the line (the text is left untouched)
//   Alt+F1..F4       "-", chs, inv, swap; the text already typed is evaluated first, as part of the same line
// "Empty" means no characters at all: a line that holds only blanks keeps Backspace, Delete and the operator keys
// as ordinary editing keys.
static intptr_t WINAPI DlgProcImpl(HANDLE hDlg, intptr_t Msg, intptr_t Param1, void* Param2)
{
    switch (Msg) {
    case DN_INITDIALOG:
        Refresh(hDlg);
        return TRUE;

    case DN_CONTROLINPUT:
        if (Param1 == ID_EDIT) {
            const INPUT_RECORD* r = (const INPUT_RECORD*)Param2;
            if (r->EventType != KEY_EVENT || !r->Event.KeyEvent.bKeyDown) break;

            const KEY_EVENT_RECORD& k = r->Event.KeyEvent;
            const bool ctrl = (k.dwControlKeyState & (LEFT_CTRL_PRESSED | RIGHT_CTRL_PRESSED)) != 0;
            const bool alt  = (k.dwControlKeyState & (LEFT_ALT_PRESSED | RIGHT_ALT_PRESSED)) != 0;
            const std::wstring raw  = GetEdit(hDlg);   // exactly what is in the line
            const std::wstring text = Trim(raw);       // without surrounding blanks

            if (ctrl && !alt && k.wVirtualKeyCode == VK_BACK) {   // Ctrl+Backspace: UNDO
                Run(L"undo");                                      // valid with text in the line; the text is left untouched
                Refresh(hDlg);
                return TRUE;
            }

            if (alt && !ctrl) {
                const wchar_t* cmd = nullptr;
                switch (k.wVirtualKeyCode) {
                case VK_F1: cmd = L"-";    break;
                case VK_F2: cmd = L"chs";  break;
                case VK_F3: cmd = L"inv";  break;
                case VK_F4: cmd = L"swap"; break;
                }
                if (cmd) {
                    // Pending text and the command run as one line, so an error rolls back both
                    // and the text is kept for correction.
                    const bool ok = Run(text.empty() ? std::wstring(cmd) : text + L" " + cmd);
                    if (ok) SetText(hDlg, ID_EDIT, L"");
                    Refresh(hDlg);
                    return TRUE;
                }
            }

            if (k.wVirtualKeyCode == VK_RETURN && !alt) {
                // Ctrl+Enter on an empty line only inserts X into the command line: no DUP, so repeating
                // it does not grow the stack.
                bool ok = true;
                if (text.empty()) {
                    g_err.clear();
                    if (!ctrl) ok = Run(L"dup");
                } else {
                    ok = Run(text);
                }
                if (ok) SetText(hDlg, ID_EDIT, L"");
                if (ok && ctrl) {
                    if (g_eng.st.empty()) {
                        g_err = GetMsg(M_STACK_EMPTY);
                    } else {
                        g_insertToCmdLine = true;   // consumed by ShowCalculator after the dialog is closed
                        PSI.SendDlgMessage(hDlg, DM_CLOSE, -1, nullptr);
                        return TRUE;
                    }
                }
                Refresh(hDlg);
                return TRUE;
            }

            // Shortcuts that only apply to an empty input line (no characters at all): a line holding only
            // blanks keeps Backspace/Del/operator keys as ordinary editing keys.
            if (raw.empty() && !ctrl && !alt) {
                if (k.wVirtualKeyCode == VK_BACK) {
                    Run(L"drop");              // on an empty stack this reports M_ERR_NOT_ENOUGH
                    Refresh(hDlg);
                    return TRUE;
                }
                if (k.wVirtualKeyCode == VK_DELETE) {
                    Run(L"clr");
                    Refresh(hDlg);
                    return TRUE;
                }
                const wchar_t ch = k.uChar.UnicodeChar;
                if (ch == L'+' || ch == L'*' || ch == L'/' || ch == L'^') {
                    Run(std::wstring(1, ch));
                    Refresh(hDlg);
                    return TRUE;
                }
            }
        }
        break;
    }
    // Everything else, including F1 (DN_HELP), is handled by Far.
    return PSI.DefDlgProc(hDlg, Msg, Param1, Param2);
}

// Boundary with Far: no C++ exception may propagate out of the dialog procedure (with the reduced
// terminate handler it would abort Far). The error is shown in the status line when possible, and the
// key is consumed.
static intptr_t WINAPI DlgProc(HANDLE hDlg, intptr_t Msg, intptr_t Param1, void* Param2)
{
    int msgId = M_ERR_INTERNAL;
    try {
        return DlgProcImpl(hDlg, Msg, Param1, Param2);
    } catch (const std::bad_alloc&) {
        msgId = M_ERR_MEMORY;
    } catch (...) {
    }
    try { g_err = GetMsg(msgId); Refresh(hDlg); } catch (...) {}   // may fail again if memory is really exhausted
    return Msg == DN_CONTROLINPUT ? TRUE : FALSE;
}

// Builds a zero-initialized FarDialogItem with the given type, rectangle, flags and text.
static FarDialogItem MakeItem(FARDIALOGITEMTYPES type, int x1, int y1, int x2, int y2,
                              unsigned long long flags, const wchar_t* data)
{
    FarDialogItem it;
    std::memset(&it, 0, sizeof(it));
    it.Type = type;
    it.X1 = x1; it.Y1 = y1; it.X2 = x2; it.Y2 = y2;
    it.Flags = flags;
    it.Data = data;
    return it;
}

// Runs the modal calculator dialog, saves the state on exit and, if requested with Ctrl+Enter,
// inserts X into the Far command line.
static void ShowCalculator()
{
    LoadState();
    g_err.clear();
    g_insertToCmdLine = false;

    FarDialogItem items[ID_COUNT];
    items[ID_BOX] = MakeItem(DI_DOUBLEBOX, 3, 1, DW - 4, DH - 2, 0, GetMsg(M_TITLE));
    for (int i = 0; i < LEVELS; i++)
        items[ID_STACK0 + i] = MakeItem(DI_TEXT, 5, 2 + i, DW - 6, 2 + i, 0, L"");
    items[ID_SEP]    = MakeItem(DI_TEXT, -1, 2 + LEVELS, 0, 2 + LEVELS, DIF_SEPARATOR, L"");
    items[ID_EDIT]   = MakeItem(DI_EDIT, 5, 3 + LEVELS, DW - 6, 3 + LEVELS, DIF_FOCUS, L"");
    items[ID_STATUS] = MakeItem(DI_TEXT, 5, 4 + LEVELS, DW - 6, 4 + LEVELS, 0, L"");
    items[ID_HINT]   = MakeItem(DI_TEXT, 5, 5 + LEVELS, DW - 6, 5 + LEVELS, 0,
                                GetMsg(M_HINT1));
    items[ID_HINT2]  = MakeItem(DI_TEXT, 5, 6 + LEVELS, DW - 6, 6 + LEVELS, 0,
                                GetMsg(M_HINT2));

    // HELP_MAIN is the help topic opened by F1 in the dialog.
    HANDLE h = PSI.DialogInit(&PluginGuid, &DialogGuid, -1, -1, DW, DH,
                              HELP_MAIN, items, ID_COUNT, 0, FDLG_NONE, DlgProc, nullptr);
    if (h != INVALID_HANDLE_VALUE) {
        PSI.DialogRun(h);
        PSI.DialogFree(h);
    }
    SaveState();

    if (g_insertToCmdLine && !g_eng.st.empty()) {
        std::wstring v = FormatExact(g_eng.st.back());
        PSI.PanelControl(PANEL_ACTIVE, FCTL_INSERTCMDLINE, 0, (void*)v.c_str());
    }
}

// ==================== rpn: <expression> ====================
// Handles a line typed in the Far command line with the "rpn:" prefix. The result (or the error)
// is reported in a message box; the state is saved only after a successful evaluation.
static void RunCommandLine(const wchar_t* cl)
{
    LoadState();

    std::wstring line = cl ? cl : L"";
    line = Trim(line);
    if (line.size() >= 4 && _wcsnicmp(line.c_str(), L"rpn:", 4) == 0) line = Trim(line.substr(4));   // the prefix may or may not be included

    std::wstring text;
    unsigned long long flags = FMSG_MB_OK | FMSG_ALLINONE;
    if (Run(line)) {
        text = std::wstring(GetMsg(M_TITLE)) + L"\n";
        if (g_eng.st.empty()) {
            text += GetMsg(M_STACK_EMPTY);
        } else {
            text += FormatMsg(M_RESULT, FormatItem(g_eng.st.back()));
            text += L"\n" + FormatMsg(M_STACK_DEPTH, Utow(g_eng.st.size()));
        }
        SaveState();
    } else {
        text = std::wstring(GetMsg(M_TITLE)) + L"\n" + g_err;
        flags |= FMSG_WARNING;
    }
    // With FMSG_ALLINONE the Items argument is a single string with lines separated by '\n'.
    PSI.Message(&PluginGuid, &MsgGuid, flags, HELP_CMDLINE, (const wchar_t* const*)text.c_str(), 0, 0);
}

// ==================== plugin exports ====================
// Minimum Far 3 build declared in GetGlobalInfoW: Far does not load the plug-in on an older build. It does not
// depend on the plugin.hpp used to compile. Can be overridden with -DRPN_MIN_FAR_BUILD=<build>.
#ifndef RPN_MIN_FAR_BUILD
#define RPN_MIN_FAR_BUILD 4000
#endif

EXPORT void WINAPI GetGlobalInfoW(GlobalInfo* Info)
{
    Info->StructSize     = sizeof(GlobalInfo);
    Info->MinFarVersion  = MAKEFARVERSION(FARMANAGERVERSION_MAJOR, FARMANAGERVERSION_MINOR,
                                          FARMANAGERVERSION_REVISION, RPN_MIN_FAR_BUILD,
                                          VS_RELEASE);
    Info->Version        = MAKEFARVERSION(PLUGIN_VER_MAJOR, PLUGIN_VER_MINOR, PLUGIN_VER_PATCH,
                                          PLUGIN_VER_BUILD, VS_RELEASE);
    Info->Guid           = PluginGuid;
    Info->Title          = PLUGIN_NAME;
    Info->Description    = PLUGIN_DESC;
    Info->Author         = PLUGIN_AUTHOR;
}

EXPORT void WINAPI SetStartupInfoW(const PluginStartupInfo* Info)
{
    // A Far older than the plugin.hpp used for compiling passes a shorter PluginStartupInfo.
    // Copy only StructSize bytes and zero the rest; the functions used here are in the common leading part.
    std::memset(&PSI, 0, sizeof(PSI));
    std::memcpy(&PSI, Info, std::min(sizeof(PSI), (size_t)Info->StructSize));
}

EXPORT void WINAPI GetPluginInfoW(PluginInfo* Info)
{
    static const wchar_t* MenuStrings[1];
    MenuStrings[0] = GetMsg(M_TITLE);

    Info->StructSize          = sizeof(PluginInfo);
    Info->Flags               = PF_NONE;
    Info->PluginMenu.Guids    = &MenuGuid;
    Info->PluginMenu.Strings  = MenuStrings;
    Info->PluginMenu.Count    = 1;
    Info->CommandPrefix       = L"rpn";
}

EXPORT HANDLE WINAPI OpenW(const OpenInfo* Info)
{
    // No C++ exception may propagate into Far (with the reduced terminate handler it would abort Far).
    try {
        if (Info->OpenFrom == OPEN_COMMANDLINE) {
            const OpenCommandLineInfo* cl = (const OpenCommandLineInfo*)Info->Data;
            RunCommandLine(cl ? cl->CommandLine : nullptr);
        } else {
            ShowCalculator();
        }
    } catch (...) {
        // Texts owned by Far or built in only: nothing here should need to allocate.
        try {
            const wchar_t* const items[] = { GetMsg(M_TITLE), GetMsg(M_ERR_FATAL) };   // title and one line
            PSI.Message(&PluginGuid, &MsgGuid, FMSG_MB_OK | FMSG_WARNING, nullptr, items, 2, 0);
        } catch (...) {}
    }
    return nullptr;   // the plug-in never opens a panel
}
