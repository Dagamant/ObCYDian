#include "markdown.h"

#include <ctype.h>

#include "display.h"
#include "theme.h"

namespace md {

namespace {

enum Style : uint16_t {
  S_BOLD = 1,
  S_ITALIC = 2,
  S_CODE = 4,
  S_STRIKE = 8,
  S_HIGHLIGHT = 16,
  S_LINK = 32,
  S_TAG = 64,
  S_UNRESOLVED = 128,
  S_MUTED = 256,
};

struct Span {
  std::string text;
  uint16_t style;
  int16_t link;
};

struct FontInfo {
  const lgfx::IFont* font;
  int16_t ascent, descent;
};

FontInfo fonts_[F_COUNT];

void initFonts() {
  if (fonts_[0].font) return;
  const lgfx::IFont* f[F_COUNT] = {font::body(),  font::bold(), font::italic(),
                                   font::boldItalic(), font::mono(), font::h1(),
                                   font::h2(),    font::bold()};
  for (int i = 0; i < F_COUNT; i++) {
    lgfx::FontMetrics m;
    f[i]->getDefaultMetric(&m);
    fonts_[i] = {f[i], m.baseline, (int16_t)(m.height - m.baseline)};
  }
}

int textWidth(const char* s, size_t len, uint8_t f) {
  char buf[128];
  int total = 0;
  while (len > 0) {
    size_t n = len < sizeof(buf) - 1 ? len : sizeof(buf) - 1;
    memcpy(buf, s, n);
    buf[n] = 0;
    total += gfx.textWidth(buf, fonts_[f].font);
    s += n;
    len -= n;
  }
  return total;
}

constexpr int kLineGap = 5;
constexpr int kParaGap = 10;
constexpr int kListIndent = 22;
constexpr int kQuoteIndent = 14;

// ---------------------------------------------------------------------------
// Line builder: accumulates runs for one visual line, wraps words, and assigns
// vertical positions when the line is finished.

class Builder {
 public:
  Builder(Doc& d, int width) : d_(d), right_(width - theme::MARGIN) {}

  int32_t y = 4;

  // Start a new source line: first visual line begins at x0, wrapped lines at hang.
  void begin(int x0, int hang) {
    x_ = x0;
    lineStart_ = x0;
    hang_ = hang;
  }

  // Decoration placed relative to the first visual line (bullets, checkboxes).
  struct LineDeco {
    uint8_t type;
    int16_t x, w;
    uint16_t color;
  };
  void addFirstLineDeco(LineDeco d) { firstDecos_.push_back(d); }
  // Decoration repeated on every visual line of the current source line (quote bars, code bg).
  void addBlockDeco(LineDeco d) { blockDecos_.push_back(d); }
  void clearBlockDecos() { blockDecos_.clear(); }

  void addText(const std::string& s, uint8_t font, uint16_t fg, uint16_t bg, uint8_t flags,
               int16_t link, bool charWrap = false) {
    size_t i = 0, n = s.size();
    while (i < n) {
      size_t j = i;
      bool space = s[i] == ' ';
      if (charWrap) {
        j = i + 1;
      } else {
        while (j < n && (s[j] == ' ') == space) j++;
      }
      const char* tok = s.data() + i;
      size_t len = j - i;
      int w = textWidth(tok, len, font);
      if (space && !charWrap) {
        if (x_ > lineStart_ || line_.empty()) append(tok, len, w, font, fg, bg, flags, link);
      } else if (x_ + w > right_ && x_ > lineStart_) {
        wrap();
        if (w > right_ - x_) {
          // Too long for any line: break it up character by character
          addText(std::string(tok, len), font, fg, bg, flags, link, true);
        } else {
          append(tok, len, w, font, fg, bg, flags, link);
        }
      } else {
        append(tok, len, w, font, fg, bg, flags, link);
      }
      i = j;
    }
  }

  // Finishes the current source line.
  void end() {
    finishLine();
    firstDecos_.clear();
  }

  void space(int px) { y += px; }

  void deco(DecoType t, int x, int32_t yy, int w, int h, uint16_t color) {
    d_.decos.push_back({yy, (int16_t)x, (int16_t)w, (int16_t)h, (uint8_t)t, color});
  }

 private:
  void append(const char* tok, size_t len, int w, uint8_t font, uint16_t fg, uint16_t bg,
              uint8_t flags, int16_t link) {
    if (!line_.empty()) {
      Run& last = line_.back();
      if (last.font == font && last.fg == fg && last.bg == bg && last.flags == flags &&
          last.link == link && last.off + last.len == d_.pool.size() && last.len + len < 60000 &&
          last.x + last.w == x_) {
        d_.pool.append(tok, len);
        last.len += len;
        last.w += w;
        x_ += w;
        return;
      }
    }
    Run r{};
    r.x = x_;
    r.w = w;
    r.font = font;
    r.flags = flags;
    r.fg = fg;
    r.bg = bg;
    r.link = link;
    r.off = d_.pool.size();
    r.len = len;
    d_.pool.append(tok, len);
    line_.push_back(r);
    x_ += w;
  }

  void wrap() {
    finishLine();
    x_ = hang_;
    lineStart_ = hang_;
  }

  void finishLine() {
    int asc = fonts_[F_BODY].ascent, desc = fonts_[F_BODY].descent;
    for (auto& r : line_) {
      asc = std::max<int>(asc, fonts_[r.font].ascent);
      desc = std::max<int>(desc, fonts_[r.font].descent);
    }
    const int h = asc + desc;
    for (auto& r : line_) {
      r.top = y + asc - fonts_[r.font].ascent;
      r.h = fonts_[r.font].ascent + fonts_[r.font].descent;
      // Trim trailing spaces from the measured width so backgrounds don't overhang
      while (r.len > 0 && d_.pool[r.off + r.len - 1] == ' ' && &r == &line_.back()) {
        r.w -= textWidth(" ", 1, r.font);
        r.len--;
      }
      d_.runs.push_back(r);
    }
    const int adv = h + kLineGap;
    for (auto& b : blockDecos_) deco((DecoType)b.type, b.x, y - kLineGap / 2 - 1, b.w, adv + 1, b.color);
    for (auto& f : firstDecos_) {
      const int cy = y + asc - fonts_[F_BODY].ascent / 2 + 1;
      if (f.type == D_BULLET) {
        deco(D_BULLET, f.x, cy, 3, 3, f.color);
      } else {
        deco((DecoType)f.type, f.x, cy - 7, 14, 14, f.color);
      }
    }
    firstDecos_.clear();
    line_.clear();
    y += adv;
  }

  Doc& d_;
  int right_;
  int x_ = 0, lineStart_ = 0, hang_ = 0;
  std::vector<Run> line_;
  std::vector<LineDeco> firstDecos_, blockDecos_;
};

// ---------------------------------------------------------------------------
// Inline parsing

bool isWordChar(char c) { return isalnum((unsigned char)c); }

class InlineParser {
 public:
  InlineParser(Doc& d, const Resolver& r) : d_(d), resolve_(r) {}

  std::vector<Span> parse(const std::string& s, uint16_t base) {
    out_.clear();
    cur_.clear();
    uint16_t style = base;
    size_t n = s.size(), i = 0;
    auto has = [&](const char* tok, size_t from) { return s.find(tok, from) != std::string::npos; };
    while (i < n) {
      char c = s[i];
      if (c == '\\' && i + 1 < n && ispunct((unsigned char)s[i + 1])) {
        cur_ += s[i + 1];
        i += 2;
        continue;
      }
      if (c == '`') {
        size_t j = s.find('`', i + 1);
        if (j != std::string::npos) {
          flush(style);
          out_.push_back({s.substr(i + 1, j - i - 1), (uint16_t)(style | S_CODE), -1});
          i = j + 1;
          continue;
        }
      }
      bool embed = s.compare(i, 3, "![[") == 0;
      if (embed || s.compare(i, 2, "[[") == 0) {
        size_t start = i + (embed ? 3 : 2);
        size_t j = s.find("]]", start);
        if (j != std::string::npos) {
          flush(style);
          wikilink(s.substr(start, j - start), style, embed);
          i = j + 2;
          continue;
        }
      }
      if (c == '[') {
        size_t close = s.find("](", i + 1);
        size_t end = close == std::string::npos ? close : s.find(')', close + 2);
        if (end != std::string::npos) {
          flush(style);
          mdlink(s.substr(i + 1, close - i - 1), s.substr(close + 2, end - close - 2), style);
          i = end + 1;
          continue;
        }
      }
      if ((c == '*' || c == '_') && i + 1 < n && s[i + 1] == c) {
        const char tok[3] = {c, c, 0};
        if ((style & S_BOLD) || has(tok, i + 2)) {
          flush(style);
          style ^= S_BOLD;
          i += 2;
          continue;
        }
      }
      if (c == '*' || c == '_') {
        bool boundary = c == '*' || ((style & S_ITALIC) ? (i + 1 >= n || !isWordChar(s[i + 1]))
                                                         : (i == 0 || !isWordChar(s[i - 1])));
        const char tok[2] = {c, 0};
        if (boundary && ((style & S_ITALIC) || (i + 1 < n && s[i + 1] != ' ' && has(tok, i + 1)))) {
          flush(style);
          style ^= S_ITALIC;
          i += 1;
          continue;
        }
      }
      if ((c == '~' || c == '=') && i + 1 < n && s[i + 1] == c) {
        const char tok[3] = {c, c, 0};
        uint16_t bit = c == '~' ? S_STRIKE : S_HIGHLIGHT;
        if ((style & bit) || has(tok, i + 2)) {
          flush(style);
          style ^= bit;
          i += 2;
          continue;
        }
      }
      if (c == '#' && (i == 0 || s[i - 1] == ' ') && i + 1 < n &&
          (isalpha((unsigned char)s[i + 1]) || s[i + 1] == '_')) {
        size_t j = i + 1;
        while (j < n && (isalnum((unsigned char)s[j]) || strchr("-_/", s[j]))) j++;
        flush(style);
        out_.push_back({s.substr(i, j - i), (uint16_t)(style | S_TAG), -1});
        i = j;
        continue;
      }
      cur_ += c;
      i++;
    }
    flush(style);
    return out_;
  }

 private:
  void flush(uint16_t style) {
    if (!cur_.empty()) out_.push_back({cur_, style, -1});
    cur_.clear();
  }

  int16_t addLink(const std::string& target, bool external) {
    Link l{target, external ? "" : resolve_(target), external};
    d_.links.push_back(l);
    return (int16_t)(d_.links.size() - 1);
  }

  void wikilink(const std::string& inner, uint16_t style, bool embed) {
    std::string target = inner, alias;
    size_t bar = inner.find('|');
    if (bar != std::string::npos) {
      target = inner.substr(0, bar);
      alias = inner.substr(bar + 1);
    }
    std::string display = alias;
    if (display.empty()) {
      display = target;
      size_t h = display.find('#');
      if (h != std::string::npos) display.replace(h, 1, h == 0 ? "" : " > ");
    }
    std::string note = target.substr(0, target.find('#'));
    int16_t idx = addLink(note, false);
    uint16_t st = style | S_LINK;
    if (d_.links[idx].resolved.empty()) st |= S_UNRESOLVED;
    out_.push_back({(embed ? "> " : "") + display, st, idx});
  }

  void mdlink(const std::string& text, const std::string& url, uint16_t style) {
    bool external = url.find("://") != std::string::npos || url.rfind("mailto:", 0) == 0;
    std::string target = url;
    if (!external) {
      // URL-decode %20 etc. and drop any #heading
      std::string dec;
      for (size_t i = 0; i < target.size(); i++) {
        if (target[i] == '%' && i + 2 < target.size()) {
          dec += (char)strtol(target.substr(i + 1, 2).c_str(), nullptr, 16);
          i += 2;
        } else {
          dec += target[i];
        }
      }
      target = dec.substr(0, dec.find('#'));
    }
    int16_t idx = addLink(target, external);
    uint16_t st = style | S_LINK;
    if (!external && d_.links[idx].resolved.empty()) st |= S_UNRESOLVED;
    out_.push_back({text.empty() ? url : text, st, idx});
  }

  Doc& d_;
  const Resolver& resolve_;
  std::vector<Span> out_;
  std::string cur_;
};

// ---------------------------------------------------------------------------
// Block parsing

std::string ltrim(const std::string& s, int* indent = nullptr) {
  size_t i = 0;
  int w = 0;
  while (i < s.size() && (s[i] == ' ' || s[i] == '\t')) {
    w += s[i] == '\t' ? 4 : 1;
    i++;
  }
  if (indent) *indent = w;
  return s.substr(i);
}

std::string rtrim(std::string s) {
  while (!s.empty() && (s.back() == ' ' || s.back() == '\t')) s.pop_back();
  return s;
}

bool isHr(const std::string& t) {
  if (t.size() < 3 || !strchr("-*_", t[0])) return false;
  int count = 0;
  for (char c : t) {
    if (c == t[0]) count++;
    else if (c != ' ') return false;
  }
  return count >= 3;
}

uint16_t calloutColor(const std::string& type) {
  std::string t;
  for (char c : type) t += tolower(c);
  if (t == "warning" || t == "caution" || t == "attention") return rgb(0xe9973f);
  if (t == "danger" || t == "error" || t == "bug" || t == "failure") return rgb(0xe93147);
  if (t == "tip" || t == "hint" || t == "important" || t == "success" || t == "check" || t == "done")
    return rgb(0x08b94e);
  if (t == "question" || t == "help" || t == "faq") return rgb(0xec7500);
  if (t == "example") return rgb(0x7852ee);
  if (t == "quote" || t == "cite") return theme::MUTED;
  return rgb(0x027aff);
}

uint8_t fontFor(uint16_t style, int heading) {
  if (style & S_CODE) return F_MONO;
  if (heading == 1) return F_H1;
  if (heading == 2) return F_H2;
  if (heading >= 3) return F_H3;
  bool b = style & S_BOLD, it = style & S_ITALIC;
  return b && it ? F_BOLD_ITALIC : b ? F_BOLD : it ? F_ITALIC : F_BODY;
}

void emitSpans(Builder& b, const std::vector<Span>& spans, int heading, uint16_t baseFg) {
  for (auto& sp : spans) {
    uint8_t f = fontFor(sp.style, heading);
    uint16_t fg = baseFg, bg = 0;
    uint8_t flags = 0;
    if (sp.style & S_MUTED) fg = theme::MUTED;
    if (sp.style & S_CODE) {
      fg = theme::CODE_TEXT;
      bg = theme::CODE_BG;
      flags |= RF_BG;
    }
    if (sp.style & S_HIGHLIGHT) {
      fg = theme::TEXT_BRIGHT;
      bg = theme::HIGHLIGHT_BG;
      flags |= RF_BG;
    }
    if (sp.style & S_TAG) {
      fg = theme::ACCENT;
      bg = theme::ACCENT_BG;
      flags |= RF_BG;
    }
    if (sp.style & S_LINK) fg = (sp.style & S_UNRESOLVED) ? theme::ACCENT_DIM : theme::ACCENT;
    if (sp.style & S_STRIKE) flags |= RF_STRIKE;
    b.addText(sp.text, f, fg, bg, flags, sp.link);
  }
}

}  // namespace

// ---------------------------------------------------------------------------

std::string toAscii(const std::string& s) {
  std::string out;
  out.reserve(s.size());
  for (size_t i = 0; i < s.size();) {
    uint8_t c = s[i];
    if (c < 0x80) {
      if (c == '\t') out += "    ";
      else if (c != '\r') out += (char)c;
      i++;
      continue;
    }
    uint32_t cp = 0;
    int extra = c >= 0xF0 ? 3 : c >= 0xE0 ? 2 : c >= 0xC0 ? 1 : 0;
    cp = c & (0x3F >> extra);
    for (int k = 1; k <= extra && i + k < s.size(); k++) cp = (cp << 6) | (s[i + k] & 0x3F);
    i += extra + 1;
    switch (cp) {
      case 0x2018: case 0x2019: case 0x2032: out += '\''; break;
      case 0x201C: case 0x201D: case 0x2033: out += '"'; break;
      case 0x2013: case 0x2014: case 0x2212: out += '-'; break;
      case 0x2026: out += "..."; break;
      case 0x2022: case 0x00B7: out += '*'; break;
      case 0x00A0: case 0x2009: case 0x200A: out += ' '; break;
      case 0x2192: out += "->"; break;
      case 0x2190: out += "<-"; break;
      case 0x2713: case 0x2714: case 0x2705: out += 'v'; break;
      case 0x00D7: out += 'x'; break;
      case 0x00B0: out += " deg"; break;
      case 0x200B: case 0xFE0F: break;
      default: out += '?';
    }
  }
  return out;
}

void layout(const std::string& source, int width, const Resolver& resolve, Doc& d) {
  initFonts();
  d = Doc();
  const std::string src = toAscii(source);
  Builder b(d, width);
  InlineParser inl(d, resolve);
  const int M = theme::MARGIN;
  const int right = width - M;

  bool inFront = false, inCode = false;
  bool lastBlank = true;
  std::string fence;
  std::vector<int> listStack;
  std::vector<int> orderedCounters;

  size_t pos = 0, lineNo = 0;
  while (pos <= src.size()) {
    size_t nl = src.find('\n', pos);
    if (nl == std::string::npos) nl = src.size();
    std::string line = rtrim(src.substr(pos, nl - pos));
    pos = nl + 1;
    bool first = lineNo++ == 0;

    // --- YAML frontmatter, shown as a muted properties box
    if (first && line == "---") {
      inFront = true;
      b.space(2);
      b.deco(D_FILL, M, b.y, right - M, 4, theme::CODE_BG);
      b.space(6);
      continue;
    }
    if (inFront) {
      if (line == "---") {
        inFront = false;
        b.deco(D_FILL, M, b.y - kLineGap / 2, right - M, 6, theme::CODE_BG);
        b.space(14);
        lastBlank = true;
        continue;
      }
      b.addBlockDeco({D_FILL, (int16_t)M, (int16_t)(right - M), theme::CODE_BG});
      b.begin(M + 8, M + 24);
      size_t colon = line.find(':');
      if (colon != std::string::npos && line[0] != ' ') {
        b.addText(line.substr(0, colon), F_BODY, theme::MUTED, 0, 0, -1);
        b.addText("  " + ltrim(line.substr(colon + 1)), F_BODY, theme::TEXT, 0, 0, -1);
      } else {
        b.addText(line, F_BODY, theme::TEXT, 0, 0, -1);
      }
      b.end();
      b.clearBlockDecos();
      continue;
    }

    // --- Fenced code
    std::string trimmed = ltrim(line);
    if (inCode) {
      if (trimmed.rfind(fence, 0) == 0) {
        inCode = false;
        b.deco(D_FILL, M, b.y - kLineGap / 2, right - M, 8, theme::CODE_BG);
        b.space(14);
        continue;
      }
      b.addBlockDeco({D_FILL, (int16_t)M, (int16_t)(right - M), theme::CODE_BG});
      b.begin(M + 8, M + 8);
      if (!line.empty()) b.addText(line, F_MONO, theme::TEXT, 0, 0, -1, true);
      b.end();
      b.clearBlockDecos();
      continue;
    }
    if (trimmed.rfind("```", 0) == 0 || trimmed.rfind("~~~", 0) == 0) {
      inCode = true;
      fence = trimmed.substr(0, 3);
      if (!lastBlank) b.space(4);
      b.deco(D_FILL, M, b.y, right - M, 8, theme::CODE_BG);
      b.space(10);
      lastBlank = false;
      continue;
    }

    // --- Blank line
    if (trimmed.empty()) {
      if (!lastBlank) b.space(kParaGap);
      lastBlank = true;
      listStack.clear();
      orderedCounters.clear();
      continue;
    }
    lastBlank = false;

    // --- Blockquotes / callouts
    int quoteDepth = 0;
    std::string content = trimmed;
    while (!content.empty() && content[0] == '>') {
      quoteDepth++;
      content = ltrim(content.substr(1));
    }
    uint16_t barColor = theme::QUOTE_BAR;
    static uint16_t calloutBar = 0;
    if (quoteDepth > 0 && content.rfind("[!", 0) == 0 && content.find(']') != std::string::npos) {
      size_t close = content.find(']');
      std::string type = content.substr(2, close - 2);
      std::string title = ltrim(content.substr(close + 1));
      if (!title.empty() && (title[0] == '-' || title[0] == '+')) title = ltrim(title.substr(1));
      if (title.empty()) {
        title = type;
        if (!title.empty()) title[0] = toupper(title[0]);
      }
      calloutBar = calloutColor(type);
      b.addBlockDeco({D_FILL, (int16_t)M, 3, calloutBar});
      b.begin(M + kQuoteIndent, M + kQuoteIndent);
      b.addText(title, F_BOLD, calloutBar, 0, 0, -1);
      b.end();
      b.clearBlockDecos();
      continue;
    }
    if (quoteDepth == 0) calloutBar = 0;
    if (quoteDepth > 0 && calloutBar) barColor = calloutBar;
    int x0 = M + quoteDepth * kQuoteIndent;
    for (int q = 0; q < quoteDepth; q++)
      b.addBlockDeco({D_FILL, (int16_t)(M + q * kQuoteIndent), 3, barColor});
    if (quoteDepth > 0) trimmed = content;

    // --- Horizontal rule
    if (isHr(trimmed)) {
      b.space(6);
      b.deco(D_FILL, x0, b.y, right - x0, 1, theme::BORDER);
      b.space(10);
      b.clearBlockDecos();
      continue;
    }

    // --- Headings
    int h = 0;
    while (h < (int)trimmed.size() && trimmed[h] == '#') h++;
    if (h >= 1 && h <= 6 && h < (int)trimmed.size() && trimmed[h] == ' ') {
      b.space(h <= 2 ? 10 : 6);
      b.begin(x0, x0);
      uint16_t fg = h == 1 ? theme::TEXT_BRIGHT : h <= 3 ? theme::TEXT : theme::MUTED;
      emitSpans(b, inl.parse(ltrim(trimmed.substr(h)), 0), h, fg);
      b.end();
      b.space(h <= 2 ? 4 : 2);
      b.clearBlockDecos();
      listStack.clear();
      continue;
    }

    // --- Lists and tasks
    int indent = 0;
    std::string lineBody = quoteDepth > 0 ? trimmed : ltrim(line, &indent);
    bool bullet = lineBody.size() >= 2 && strchr("-*+", lineBody[0]) && lineBody[1] == ' ';
    size_t digits = 0;
    while (digits < lineBody.size() && isdigit((unsigned char)lineBody[digits])) digits++;
    bool ordered = digits > 0 && digits < 10 && digits + 1 < lineBody.size() &&
                   (lineBody[digits] == '.' || lineBody[digits] == ')') && lineBody[digits + 1] == ' ';
    if (bullet || ordered) {
      while (!listStack.empty() && indent < listStack.back()) listStack.pop_back();
      if (listStack.empty() || indent > listStack.back()) listStack.push_back(indent);
      int level = listStack.size() - 1;
      int lx = x0 + level * kListIndent;
      int tx = lx + 18;
      std::string text = ltrim(lineBody.substr(bullet ? 2 : digits + 2));
      uint16_t base = 0;
      if (bullet && text.size() >= 3 && text[0] == '[' && text[2] == ']' &&
          (text[1] == ' ' || text[1] == 'x' || text[1] == 'X')) {
        bool done = text[1] != ' ';
        text = ltrim(text.substr(3));
        tx = lx + 22;
        b.addFirstLineDeco({(uint8_t)(done ? D_CHECKBOX_DONE : D_CHECKBOX), (int16_t)(lx + 1), 14,
                            done ? theme::ACCENT : theme::MUTED});
        if (done) base = S_STRIKE | S_MUTED;
      } else if (bullet) {
        b.addFirstLineDeco({D_BULLET, (int16_t)(lx + 6), 3, theme::MUTED});
      }
      b.begin(ordered ? lx : tx, tx);
      if (ordered) {
        std::string num = lineBody.substr(0, digits + 1) + " ";
        b.addText(num, F_BODY, theme::MUTED, 0, 0, -1);
      }
      emitSpans(b, inl.parse(text, base), 0, theme::TEXT);
      b.end();
      b.clearBlockDecos();
      continue;
    }

    // --- Tables (rendered as monospace rows)
    if (trimmed[0] == '|') {
      bool sep = trimmed.find_first_not_of("|-: ") == std::string::npos;
      if (sep) {
        b.deco(D_FILL, x0, b.y - kLineGap / 2, right - x0, 1, theme::BORDER);
      } else {
        b.begin(x0, x0);
        b.addText(trimmed, F_MONO, theme::TEXT, 0, 0, -1);
        b.end();
      }
      b.clearBlockDecos();
      continue;
    }

    // --- Paragraph line (Obsidian keeps single newlines as line breaks)
    int px = quoteDepth > 0 ? x0 : (listStack.empty() ? x0 : x0 + std::min(indent, 8) * 4);
    b.begin(px, px);
    emitSpans(b, inl.parse(trimmed, 0), 0, theme::TEXT);
    b.end();
    b.clearBlockDecos();
  }
  d.height = b.y + 16;
}

void render(LGFX_Sprite& s, const Doc& d, int32_t top) {
  initFonts();
  const int32_t bottom = top + s.height();
  for (auto& dc : d.decos) {
    if (dc.y + dc.h < top || dc.y > bottom) continue;
    int y = dc.y - top;
    switch (dc.type) {
      case D_FILL: s.fillRect(dc.x, y, dc.w, dc.h, dc.color); break;
      case D_ROUND_FILL: s.fillRoundRect(dc.x, y, dc.w, dc.h, 3, dc.color); break;
      case D_HLINE: s.drawFastHLine(dc.x, y, dc.w, dc.color); break;
      case D_BULLET: s.fillCircle(dc.x, y, dc.w, dc.color); break;
      case D_CHECKBOX: s.drawRoundRect(dc.x, y, dc.w, dc.h, 3, dc.color); break;
      case D_CHECKBOX_DONE:
        s.fillRoundRect(dc.x, y, dc.w, dc.h, 3, dc.color);
        s.drawLine(dc.x + 3, y + 7, dc.x + 6, y + 10, theme::BG);
        s.drawLine(dc.x + 6, y + 10, dc.x + 11, y + 4, theme::BG);
        s.drawLine(dc.x + 3, y + 8, dc.x + 6, y + 11, theme::BG);
        s.drawLine(dc.x + 6, y + 11, dc.x + 11, y + 5, theme::BG);
        break;
    }
  }
  s.setTextDatum(textdatum_t::baseline_left);
  char buf[256];
  for (auto& r : d.runs) {
    if (r.top > bottom) break;
    if (r.top + r.h < top) continue;
    int y = r.top - top;
    if (r.flags & RF_BG) s.fillRoundRect(r.x - 2, y, r.w + 4, r.h, 3, r.bg);
    size_t n = std::min<size_t>(r.len, sizeof(buf) - 1);
    memcpy(buf, d.pool.data() + r.off, n);
    buf[n] = 0;
    s.setFont(fonts_[r.font].font);
    s.setTextColor(r.fg);
    s.drawString(buf, r.x, y + fonts_[r.font].ascent);
    if (r.flags & RF_STRIKE) {
      s.drawFastHLine(r.x, y + fonts_[r.font].ascent * 2 / 3, r.w, r.fg);
    }
  }
}

int linkAt(const Doc& d, int x, int32_t y) {
  constexpr int slop = 6;
  int best = -1, bestDist = 1 << 30;
  for (auto& r : d.runs) {
    if (r.link < 0) continue;
    if (x < r.x - slop || x > r.x + r.w + slop || y < r.top - slop || y > r.top + r.h + slop) continue;
    int cx = std::max(0, std::max(r.x - x, x - (r.x + r.w)));
    int cy = std::max<int32_t>(0, std::max<int32_t>(r.top - y, y - (r.top + r.h)));
    if (cx + cy < bestDist) {
      bestDist = cx + cy;
      best = r.link;
    }
  }
  return best;
}

}  // namespace md
