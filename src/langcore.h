#pragma once

// Nucleo do sistema de idiomas do Notbad.
//
// Nao usa nada do Win32: so a biblioteca padrao. Isso permite testar a logica
// fora do Windows (tools/test_langcore.cpp). A parte que conversa com o
// sistema (idioma do Windows, arquivos, recursos) fica em lang.cpp.
//
// O tipo de texto e um parametro (S = std::wstring no Notbad). No Windows
// wchar_t tem 16 bits (UTF-16); os testes tambem usam char32_t.

#include <cstddef>
#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

namespace langcore {

// ---------------------------------------------------------------------------
// Texto
// ---------------------------------------------------------------------------

// Acrescenta um code point. Em texto de 16 bits, fora do BMP vira par
// substituto. Substitutos soltos viram U+FFFD.
template <class S>
void AppendCodePoint(S& out, uint32_t cp)
{
    typedef typename S::value_type C;

    if (cp >= 0xD800 && cp <= 0xDFFF)
        cp = 0xFFFD;

    if (sizeof(C) >= 4 || cp < 0x10000)
    {
        out.push_back((C)cp);
        return;
    }

    cp -= 0x10000;
    out.push_back((C)(0xD800 + (cp >> 10)));
    out.push_back((C)(0xDC00 + (cp & 0x3FF)));
}

// Decodifica um caractere UTF-8 estrito (sem sequencias curtas demais, sem
// substitutos, ate U+10FFFF). Avanca *pi.
inline bool DecodeUtf8(const unsigned char* p, size_t n, size_t* pi, uint32_t* pcp)
{
    size_t i = *pi;
    unsigned char c = p[i];
    uint32_t cp;
    size_t extra;
    size_t k;

    if (c < 0x80)
    {
        *pcp = c;
        *pi = i + 1;
        return true;
    }

    if (c >= 0xC2 && c <= 0xDF)      { cp = c & 0x1F; extra = 1; }
    else if (c >= 0xE0 && c <= 0xEF) { cp = c & 0x0F; extra = 2; }
    else if (c >= 0xF0 && c <= 0xF4) { cp = c & 0x07; extra = 3; }
    else return false;

    if (i + extra >= n)
        return false;

    for (k = 1; k <= extra; ++k)
    {
        unsigned char c2 = p[i + k];
        if ((c2 & 0xC0) != 0x80)
            return false;
        cp = (cp << 6) | (uint32_t)(c2 & 0x3F);
    }

    if (extra == 2 && cp < 0x800)
        return false;
    if (extra == 3 && (cp < 0x10000 || cp > 0x10FFFF))
        return false;
    if (cp >= 0xD800 && cp <= 0xDFFF)
        return false;

    *pcp = cp;
    *pi = i + extra + 1;
    return true;
}

// ---------------------------------------------------------------------------
// JSON plano: { "chave": "texto", ... }
//
// - Valores precisam ser texto. Qualquer outra coisa invalida o arquivo.
// - Chaves iniciadas por "_" sao metadados e nao entram no mapa.
// - Aceita BOM UTF-8 e virgula sobrando antes do "}".
// - Chave repetida: vale a ultima.
// - Em caso de erro o mapa de saida fica vazio e Error() diz o motivo.
// ---------------------------------------------------------------------------

template <class S>
class FlatJsonParser
{
public:
    typedef std::unordered_map<S, S> Map;

    FlatJsonParser(const char* data, size_t size)
        : p_(reinterpret_cast<const unsigned char*>(data)), n_(size), i_(0), err_("")
    {
    }

    bool Parse(Map& out)
    {
        Map tmp;
        S key;
        S value;

        out.clear();
        i_ = 0;
        err_ = "";

        if (p_ == NULL)
            return Fail("sem dados");

        if (n_ >= 3 && p_[0] == 0xEF && p_[1] == 0xBB && p_[2] == 0xBF)
            i_ = 3;

        SkipWs();
        if (i_ >= n_ || p_[i_] != '{')
            return Fail("esperado '{'");
        ++i_;

        SkipWs();
        if (i_ < n_ && p_[i_] == '}')
        {
            ++i_;
        }
        else
        {
            for (;;)
            {
                SkipWs();
                if (i_ >= n_ || p_[i_] != '"')
                    return Fail("esperada chave entre aspas");
                key.clear();
                if (!ParseString(key))
                    return false;

                SkipWs();
                if (i_ >= n_ || p_[i_] != ':')
                    return Fail("esperado ':'");
                ++i_;

                SkipWs();
                if (i_ >= n_ || p_[i_] != '"')
                    return Fail("o valor precisa ser texto");
                value.clear();
                if (!ParseString(value))
                    return false;

                if (!key.empty() && key[0] != (typename S::value_type)'_')
                    tmp[key] = value;

                SkipWs();
                if (i_ >= n_)
                    return Fail("objeto nao terminado");

                if (p_[i_] == ',')
                {
                    ++i_;
                    SkipWs();
                    if (i_ < n_ && p_[i_] == '}')
                    {
                        ++i_;
                        break;
                    }
                    continue;
                }

                if (p_[i_] == '}')
                {
                    ++i_;
                    break;
                }

                return Fail("esperado ',' ou '}'");
            }
        }

        SkipWs();
        if (i_ != n_)
            return Fail("texto depois do objeto");

        out.swap(tmp);
        return true;
    }

    const char* Error() const { return err_; }

private:
    bool Fail(const char* msg)
    {
        err_ = msg;
        return false;
    }

    void SkipWs()
    {
        while (i_ < n_ && (p_[i_] == ' ' || p_[i_] == '\t' || p_[i_] == '\r' || p_[i_] == '\n'))
            ++i_;
    }

    bool ParseHex4(uint32_t* pv)
    {
        uint32_t v = 0;
        int k;

        if (i_ + 4 > n_)
            return Fail("escape \\u incompleto");

        for (k = 0; k < 4; ++k)
        {
            unsigned char c = p_[i_ + (size_t)k];
            v <<= 4;
            if (c >= '0' && c <= '9')      v |= (uint32_t)(c - '0');
            else if (c >= 'a' && c <= 'f') v |= (uint32_t)(c - 'a' + 10);
            else if (c >= 'A' && c <= 'F') v |= (uint32_t)(c - 'A' + 10);
            else return Fail("escape \\u invalido");
        }

        i_ += 4;
        *pv = v;
        return true;
    }

    // Em p_[i_] ha a aspa de abertura.
    bool ParseString(S& out)
    {
        ++i_;

        for (;;)
        {
            unsigned char c;

            if (i_ >= n_)
                return Fail("texto nao terminado");

            c = p_[i_];

            if (c == '"')
            {
                ++i_;
                return true;
            }

            if (c == '\\')
            {
                unsigned char e;

                ++i_;
                if (i_ >= n_)
                    return Fail("texto nao terminado");

                e = p_[i_++];
                switch (e)
                {
                case '"':  AppendCodePoint(out, '"');  break;
                case '\\': AppendCodePoint(out, '\\'); break;
                case '/':  AppendCodePoint(out, '/');  break;
                case 'b':  AppendCodePoint(out, 8);    break;
                case 'f':  AppendCodePoint(out, 12);   break;
                case 'n':  AppendCodePoint(out, 10);   break;
                case 'r':  AppendCodePoint(out, 13);   break;
                case 't':  AppendCodePoint(out, 9);    break;
                case 'u':
                {
                    uint32_t u;

                    if (!ParseHex4(&u))
                        return false;

                    if (u >= 0xD800 && u <= 0xDBFF)
                    {
                        // Substituto alto: precisa de \uDC00..\uDFFF logo depois.
                        size_t save = i_;
                        uint32_t lo = 0;

                        if (i_ + 1 < n_ && p_[i_] == '\\' && p_[i_ + 1] == 'u')
                        {
                            i_ += 2;
                            if (ParseHex4(&lo) && lo >= 0xDC00 && lo <= 0xDFFF)
                            {
                                AppendCodePoint(out, 0x10000 + ((u - 0xD800) << 10) + (lo - 0xDC00));
                                break;
                            }
                            err_ = "";
                        }

                        i_ = save;
                        AppendCodePoint(out, 0xFFFD);
                    }
                    else
                    {
                        AppendCodePoint(out, u);   // substituto baixo solto vira U+FFFD
                    }
                    break;
                }
                default:
                    return Fail("escape invalido");
                }
                continue;
            }

            {
                uint32_t cp;

                if (!DecodeUtf8(p_, n_, &i_, &cp))
                    return Fail("UTF-8 invalido");
                AppendCodePoint(out, cp);
            }
        }
    }

    const unsigned char* p_;
    size_t n_;
    size_t i_;
    const char* err_;
};

template <class S>
bool ParseFlatJson(const char* data, size_t size,
                   std::unordered_map<S, S>& out, const char** ppError = NULL)
{
    FlatJsonParser<S> parser(data, size);
    bool ok = parser.Parse(out);

    if (ppError != NULL)
        *ppError = parser.Error();
    return ok;
}

// ---------------------------------------------------------------------------
// Formatadores printf
//
// As mensagens da stringtable que levam %s/%d/%u sao passadas direto para
// StringCchPrintfW. Uma traducao com formatador faltando, sobrando ou de outro
// tipo corromperia a pilha, entao so vale se tiver exatamente as mesmas
// conversoes do ingles, na mesma ordem.
// ---------------------------------------------------------------------------

// Sequencia de conversoes do texto, ex.: "s;d;" ("%%" e ignorado). Retorna
// false para o que nao e suportado: '%' no fim, '*', "%1$s", "%n"...
template <class S>
bool ExtractFormatSpecs(const S& s, std::string* pSpecs)
{
    typedef typename S::value_type C;
    static const char kConversions[] = "cCdiouxXsSpeEfgG";
    size_t i = 0;
    size_t n = s.size();
    std::string specs;

    while (i < n)
    {
        std::string spec;

        if (s[i] != (C)'%')
        {
            ++i;
            continue;
        }

        ++i;
        if (i >= n)
            return false;

        if (s[i] == (C)'%')
        {
            ++i;
            continue;
        }

        while (i < n && (s[i] == (C)'-' || s[i] == (C)'+' || s[i] == (C)' ' ||
                         s[i] == (C)'#' || s[i] == (C)'0'))
            ++i;

        while (i < n && s[i] >= (C)'0' && s[i] <= (C)'9')
            ++i;
        if (i < n && s[i] == (C)'*')
            return false;

        if (i < n && s[i] == (C)'.')
        {
            ++i;
            while (i < n && s[i] >= (C)'0' && s[i] <= (C)'9')
                ++i;
            if (i < n && s[i] == (C)'*')
                return false;
        }

        while (i < n && (s[i] == (C)'h' || s[i] == (C)'l' || s[i] == (C)'L' || s[i] == (C)'w' ||
                         s[i] == (C)'z' || s[i] == (C)'j' || s[i] == (C)'t' || s[i] == (C)'I'))
        {
            bool isI = (s[i] == (C)'I');

            spec.push_back((char)s[i]);
            ++i;
            if (isI)
            {
                while (i < n && s[i] >= (C)'0' && s[i] <= (C)'9')
                {
                    spec.push_back((char)s[i]);
                    ++i;
                }
            }
        }

        if (i >= n)
            return false;

        {
            bool known = false;
            size_t k;

            for (k = 0; kConversions[k] != '\0'; ++k)
            {
                if (s[i] == (C)kConversions[k])
                {
                    known = true;
                    break;
                }
            }
            if (!known)
                return false;
        }

        spec.push_back((char)s[i]);
        ++i;
        specs += spec;
        specs.push_back(';');
    }

    *pSpecs = specs;
    return true;
}

// O texto traduzido tem as mesmas conversoes do texto base?
template <class S>
bool SameFormatSpecs(const S& base, const S& translated)
{
    std::string a;
    std::string b;

    if (!ExtractFormatSpecs(base, &a) || !ExtractFormatSpecs(translated, &b))
        return false;
    return a == b;
}

// ---------------------------------------------------------------------------
// Idiomas
// ---------------------------------------------------------------------------

inline bool IsAsciiAlnum(char c)
{
    return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z');
}

inline char AsciiLower(char c)
{
    return (c >= 'A' && c <= 'Z') ? (char)(c - 'A' + 'a') : c;
}

inline char AsciiUpper(char c)
{
    return (c >= 'a' && c <= 'z') ? (char)(c - 'a' + 'A') : c;
}

inline bool SameTagIgnoreCase(const std::string& a, const std::string& b)
{
    size_t i;

    if (a.size() != b.size())
        return false;
    for (i = 0; i < a.size(); ++i)
    {
        if (AsciiLower(a[i]) != AsciiLower(b[i]))
            return false;
    }
    return true;
}

// "pt_BR" -> "pt-BR". Retorna "" se nao for um tag de idioma plausivel
// (letras, numeros e '-', sem subtag vazio, ate 35 caracteres).
inline std::string NormalizeLangTag(const std::string& tag)
{
    std::string t;
    size_t i;

    if (tag.empty() || tag.size() > 35)
        return std::string();

    for (i = 0; i < tag.size(); ++i)
    {
        char c = tag[i];

        if (c == '_')
            c = '-';
        if (!IsAsciiAlnum(c) && c != '-')
            return std::string();
        t.push_back(c);
    }

    if (t[0] == '-' || t[t.size() - 1] == '-' || t.find("--") != std::string::npos)
        return std::string();

    return t;
}

inline void AddUniqueTag(std::vector<std::string>& list, const std::string& tag)
{
    size_t i;

    for (i = 0; i < list.size(); ++i)
    {
        if (SameTagIgnoreCase(list[i], tag))
            return;
    }
    list.push_back(tag);
}

// Ordem de procura para um idioma do sistema: o tag exato, depois cada
// versao mais geral ("es-AR" -> "es"), e por ultimo o ingles.
//   "pt-BR"      -> pt-BR, pt, en
//   "zh-Hans-CN" -> zh-Hans-CN, zh-Hans, zh, en
//   "en-US"      -> en-US, en
//   ""           -> en
inline std::vector<std::string> BuildLangChain(const std::string& tag)
{
    std::vector<std::string> chain;
    std::string t = NormalizeLangTag(tag);

    while (!t.empty())
    {
        size_t pos;

        AddUniqueTag(chain, t);
        pos = t.rfind('-');
        if (pos == std::string::npos)
            break;
        t.erase(pos);
    }

    AddUniqueTag(chain, "en");
    return chain;
}

// Nome do recurso RCDATA embutido para um idioma: "pt-BR" -> "NBLANG_PT_BR".
inline std::string ResourceNameFor(const std::string& tag)
{
    std::string name = "NBLANG_";
    size_t i;

    for (i = 0; i < tag.size(); ++i)
        name.push_back(tag[i] == '-' ? '_' : AsciiUpper(tag[i]));
    return name;
}

} // namespace langcore
