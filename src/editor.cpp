#include "editor.h"

#include <algorithm>
#include <functional>

#include "display.h"
#include "osk.h"
#include "storage.h"
#include "textfont.h"
#include "theme.h"
#include "ui.h"

using input::Event;

namespace {

constexpr int kStatusH = 20;
constexpr int kGap = 5;
constexpr int kTabW = 24;
constexpr int kQuoteW = 14;
constexpr size_t kMaxBytes = 96 * 1024;

// Inline style bits
enum : uint16_t {
  ST_BOLD = 1,
  ST_ITALIC = 2,
  ST_CODE = 4,
  ST_STRIKE = 8,
  ST_HL = 16,
  ST_LINK = 32,
  ST_TAG = 64,
  ST_UNRES = 128,
  ST_MUTED = 256,
  ST_MONO = 512,
  ST_FAINT = 1024,
  ST_CALLOUT = 2048,  // coloured with the line's callout colour
};

// Segment kinds
enum : uint8_t { KD_TEXT, KD_MARK, KD_BULLET, KD_CHECK, KD_CHECK_DONE, KD_RULE };

// Line block kinds
enum : uint8_t { BL_TEXT, BL_CODE, BL_FENCE, BL_FRONT, BL_COMMENT, BL_EMBED, BL_IMAGE };
constexpr int kImageMaxH = 220;

// Block state carried from line to line
enum : uint8_t { SS_CODE = 1, SS_FRONT = 2, SS_TILDE = 4, SS_COMMENT = 64 };
// Bits 3-5 of the line state carry the callout type through the lines of a callout
constexpr int kCalloutShift = 3;

// Obsidian callout types grouped by colour: 1 note, 2 tip, 3 warning, 4 danger,
// 5 question, 6 example, 7 quote
int calloutIndex(const char* t, size_t n) {
  std::string s(t, n);
  for (auto& c : s) c = tolower((unsigned char)c);
  static const struct { const char* name; int idx; } kTypes[] = {
      {"note", 1},     {"info", 1},      {"todo", 1},     {"abstract", 1}, {"summary", 1}, {"tldr", 1},
      {"tip", 2},      {"hint", 2},      {"important", 2}, {"success", 2}, {"check", 2},   {"done", 2},
      {"warning", 3},  {"caution", 3},   {"attention", 3}, {"danger", 4},  {"error", 4},   {"bug", 4},
      {"failure", 4},  {"fail", 4},      {"missing", 4},  {"question", 5}, {"help", 5},    {"faq", 5},
      {"example", 6},  {"quote", 7},     {"cite", 7}};
  for (auto& k : kTypes)
    if (s == k.name) return k.idx;
  return 1;
}

uint16_t calloutColor(int idx) {
  static const uint16_t kColors[] = {0,           rgb(0x027aff), rgb(0x08b94e), rgb(0xe9973f),
                                     rgb(0xe93147), rgb(0xec7500), rgb(0x7852ee), rgb(0x9e9e9e)};
  return idx > 0 && idx < 8 ? kColors[idx] : theme::QUOTE_BAR;
}

// Mixes `alpha`/256 of colour c over the background (for callout tints)
uint16_t tint(uint16_t c, int alpha) {
  auto mix = [&](int a, int b) { return (a * alpha + b * (256 - alpha)) >> 8; };
  const uint16_t bg = theme::BG;
  int r = mix(c >> 11, bg >> 11), g = mix((c >> 5) & 63, (bg >> 5) & 63), b = mix(c & 31, bg & 31);
  return (r << 11) | (g << 5) | b;
}

// Finds "[!type]" (with an optional +/- fold marker) at p[pos]. Sets the type range and
// where the header ends (just past the marker and following spaces).
bool parseCallout(const char* p, uint32_t n, uint32_t pos, int* idx, uint32_t* typeA, uint32_t* typeB,
                  uint32_t* headerEnd) {
  if (pos + 3 >= n || p[pos] != '[' || p[pos + 1] != '!') return false;
  uint32_t e = pos + 2;
  while (e < n && p[e] != ']' && p[e] != ' ') e++;
  if (e >= n || p[e] != ']' || e == pos + 2) return false;
  *typeA = pos + 2;
  *typeB = e;
  *idx = calloutIndex(p + pos + 2, e - pos - 2);
  e++;
  if (e < n && (p[e] == '+' || p[e] == '-')) e++;
  while (e < n && p[e] == ' ') e++;
  *headerEnd = e;
  return true;
}

uint8_t fontFor(uint16_t style, int heading) {
  if (style & (ST_CODE | ST_MONO)) return tf::MONO;
  if (heading == 1) return tf::H1;
  if (heading == 2) return tf::H2;
  if (heading >= 3) return tf::H3;
  bool b = style & ST_BOLD, it = style & ST_ITALIC;
  return b && it ? tf::BOLD_ITALIC : b ? tf::BOLD : it ? tf::ITALIC : tf::BODY;
}

struct Colors {
  uint16_t fg, bg;
  bool hasBg;
};

Colors colorsFor(const EditorScreen::Seg& s, int heading, uint16_t callout) {
  Colors c{theme::TEXT, 0, false};
  if (s.style & ST_CALLOUT) c.fg = callout;
  if (heading == 1) c.fg = theme::TEXT_BRIGHT;
  else if (heading >= 4) c.fg = theme::MUTED;
  if (s.style & ST_MUTED) c.fg = theme::MUTED;
  if (s.style & ST_CODE) c = {theme::CODE_TEXT, theme::CODE_BG, true};
  if (s.style & ST_HL) c = {theme::TEXT_BRIGHT, theme::HIGHLIGHT_BG, true};
  if (s.style & ST_TAG) c = {theme::ACCENT, theme::ACCENT_BG, true};
  if (s.style & ST_LINK) c.fg = (s.style & ST_UNRES) ? theme::ACCENT_DIM : theme::ACCENT;
  if (s.style & ST_MONO) c.fg = theme::TEXT;
  if ((s.style & ST_FAINT) || s.kind == KD_MARK) c.fg = (s.style & ST_LINK) ? theme::ACCENT_DIM : theme::FAINT;
  return c;
}

bool startsWith(const char* p, uint32_t n, uint32_t at, const char* tok) {
  size_t l = strlen(tok);
  return at + l <= n && memcmp(p + at, tok, l) == 0;
}

bool isHr(const char* p, uint32_t n) {
  if (n < 3 || !strchr("-*_", p[0])) return false;
  int count = 0;
  for (uint32_t i = 0; i < n; i++) {
    if (p[i] == p[0]) count++;
    else if (p[i] != ' ') return false;
  }
  return count >= 3;
}

bool isWordChar(char c) { return isalnum((unsigned char)c) || (uint8_t)c >= 0x80; }

using Resolve = std::function<bool(const std::string&)>;

// Splits [a, n) of a line into styled text and syntax-marker segments.
void inlineSegs(const char* s, uint32_t a, uint32_t n, uint16_t base, const Resolve& resolved,
                std::vector<EditorScreen::Seg>& out) {
  uint16_t style = base;
  uint32_t i = a, textStart = a;
  auto flush = [&](uint32_t end) {
    if (end > textStart) out.push_back({textStart, end, style, KD_TEXT});
  };
  auto mark = [&](uint32_t from, uint32_t to, uint16_t st) { out.push_back({from, to, st, KD_MARK}); };
  auto find = [&](const char* tok, uint32_t from) -> uint32_t {
    size_t l = strlen(tok);
    for (uint32_t k = from; k + l <= n; k++)
      if (memcmp(s + k, tok, l) == 0) return k;
    return n;
  };

  while (i < n) {
    char c = s[i];
    if (c == '\\' && i + 1 < n && ispunct((unsigned char)s[i + 1])) {
      flush(i);
      mark(i, i + 1, style);
      textStart = i + 1;
      i += 2;
      continue;
    }
    if (c == '%' && i + 1 < n && s[i + 1] == '%') {  // %%inline comment%%
      uint32_t j = find("%%", i + 2);
      if (j < n) {
        flush(i);
        mark(i, j + 2, ST_FAINT);
        i = textStart = j + 2;
        continue;
      }
    }
    if (c == '`') {
      uint32_t j = find("`", i + 1);
      if (j < n) {
        flush(i);
        mark(i, i + 1, style | ST_CODE);
        if (j > i + 1) out.push_back({i + 1, j, (uint16_t)(style | ST_CODE), KD_TEXT});
        mark(j, j + 1, style | ST_CODE);
        i = textStart = j + 1;
        continue;
      }
    }
    bool embed = startsWith(s, n, i, "![[");
    if (embed || startsWith(s, n, i, "[[")) {
      uint32_t st = i + (embed ? 3 : 2);
      uint32_t j = find("]]", st);
      if (j < n) {
        flush(i);
        uint32_t bar = find("|", st);
        if (bar > j) bar = j;
        uint32_t hash = find("#", st);
        if (hash > bar) hash = bar;
        uint16_t ls = style | ST_LINK;
        if (!resolved(std::string(s + st, hash - st))) ls |= ST_UNRES;
        mark(i, st, ls);
        if (bar < j) {
          mark(st, bar + 1, ls);  // target + '|' hidden behind the alias
          if (j > bar + 1) out.push_back({bar + 1, j, ls, KD_TEXT});
        } else if (j > st) {
          out.push_back({st, j, ls, KD_TEXT});
        }
        mark(j, j + 2, ls);
        i = textStart = j + 2;
        continue;
      }
    }
    if (c == '[') {
      uint32_t close = find("](", i + 1);
      uint32_t end = close < n ? find(")", close + 2) : n;
      if (end < n) {
        flush(i);
        std::string url(s + close + 2, end - close - 2);
        bool external = url.find("://") != std::string::npos;
        uint16_t ls = style | ST_LINK;
        if (!external && !resolved(url.substr(0, url.find('#')))) ls |= ST_UNRES;
        mark(i, i + 1, ls);
        if (close > i + 1) out.push_back({i + 1, close, ls, KD_TEXT});
        mark(close, end + 1, ls);
        i = textStart = end + 1;
        continue;
      }
    }
    if ((c == '*' || c == '_') && i + 1 < n && s[i + 1] == c) {
      const char tok[3] = {c, c, 0};
      if ((style & ST_BOLD) || find(tok, i + 2) < n) {
        flush(i);
        mark(i, i + 2, style);
        style ^= ST_BOLD;
        i = textStart = i + 2;
        continue;
      }
    }
    if (c == '*' || c == '_') {
      bool closing = style & ST_ITALIC;
      bool boundary = c == '*' || (closing ? (i + 1 >= n || !isWordChar(s[i + 1]))
                                           : (i == 0 || !isWordChar(s[i - 1])));
      const char tok[2] = {c, 0};
      if (boundary && (closing || (i + 1 < n && s[i + 1] != ' ' && find(tok, i + 1) < n))) {
        flush(i);
        mark(i, i + 1, style);
        style ^= ST_ITALIC;
        i = textStart = i + 1;
        continue;
      }
    }
    if ((c == '~' || c == '=') && i + 1 < n && s[i + 1] == c) {
      const char tok[3] = {c, c, 0};
      uint16_t bit = c == '~' ? ST_STRIKE : ST_HL;
      if ((style & bit) || find(tok, i + 2) < n) {
        flush(i);
        if (!(style & bit)) {
          mark(i, i + 2, style);
          style ^= bit;
        } else {
          style ^= bit;
          mark(i, i + 2, style);
        }
        i = textStart = i + 2;
        continue;
      }
    }
    if (c == '#' && (i == 0 || s[i - 1] == ' ') && i + 1 < n &&
        (isalpha((unsigned char)s[i + 1]) || s[i + 1] == '_')) {
      uint32_t j = i + 1;
      while (j < n && (isalnum((unsigned char)s[j]) || strchr("-_/", s[j]))) j++;
      flush(i);
      out.push_back({i, j, (uint16_t)(style | ST_TAG), KD_TEXT});
      i = textStart = j;
      continue;
    }
    i++;
  }
  flush(n);
}

}  // namespace

// ===========================================================================
// Text model

uint32_t EditorScreen::lineLen(int i) const {
  uint32_t end = i + 1 < (int)lines_.size() ? lines_[i + 1].start - 1 : text_.size();
  return end - lines_[i].start;
}

int EditorScreen::lineOf(uint32_t pos) const {
  int lo = 0, hi = lines_.size() - 1;
  while (lo < hi) {
    int mid = (lo + hi + 1) / 2;
    if (lines_[mid].start <= pos) lo = mid;
    else hi = mid - 1;
  }
  return lo;
}

void EditorScreen::rebuildLines() {
  lines_.clear();
  lines_.push_back({0, 0, 0, true});
  for (uint32_t i = 0; i < text_.size(); i++)
    if (text_[i] == '\n') lines_.push_back({i + 1, 0, 0, true});
}

uint8_t EditorScreen::stateOut(uint8_t st, int i) const {
  const char* p = text_.data() + lines_[i].start;
  uint32_t n = lineLen(i);
  uint32_t fw = 0;
  while (fw < n && (p[fw] == ' ' || p[fw] == '\t')) fw++;
  if (st & SS_COMMENT) return memmem(p, n, "%%", 2) ? 0 : SS_COMMENT;  // %% ... %% block
  if (st & SS_FRONT) return (n == 3 && memcmp(p, "---", 3) == 0) ? 0 : SS_FRONT;
  if (st & SS_CODE) {
    const char* fence = (st & SS_TILDE) ? "~~~" : "```";
    return startsWith(p, n, fw, fence) ? 0 : st;
  }
  if (i == 0 && n == 3 && memcmp(p, "---", 3) == 0) return SS_FRONT;
  if (startsWith(p, n, fw, "```")) return SS_CODE;
  if (startsWith(p, n, fw, "~~~")) return SS_CODE | SS_TILDE;
  if (startsWith(p, n, fw, "%%") && !memmem(p + fw + 2, n - fw - 2, "%%", 2)) return SS_COMMENT;
  // Callouts: a quote starting "[!type]" opens one; following quote lines continue it
  if (fw < n && p[fw] == '>') {
    uint32_t pos = fw;
    while (pos < n && (p[pos] == '>' || p[pos] == ' ')) pos++;
    int idx;
    uint32_t a, b, e;
    if (parseCallout(p, n, pos, &idx, &a, &b, &e)) return idx << kCalloutShift;
    return st & (7 << kCalloutShift);
  }
  return 0;
}

bool EditorScreen::relayout() {
  uint8_t st = 0;
  for (int i = 0; i < (int)lines_.size(); i++) {
    if (lines_[i].stateIn != st) {
      lines_[i].stateIn = st;
      lines_[i].dirty = true;
    }
    st = stateOut(st, i);
  }
  bool changed = false;
  Layout tmp;
  tops_.resize(lines_.size());
  int32_t y = 4;
  for (int i = 0; i < (int)lines_.size(); i++) {
    if (lines_[i].dirty) {
      layoutLine(i, revealedLine(i), tmp);
      if (tmp.height != lines_[i].height) changed = true;
      lines_[i].height = tmp.height;
      lines_[i].dirty = false;
    }
    tops_[i] = y;
    y += lines_[i].height;
  }
  totalH_ = y + 40;
  return changed;
}

bool EditorScreen::resolved(const std::string& target) {
  if (linkGen_ != storage::generation()) {
    linkCache_.clear();
    linkGen_ = storage::generation();
  }
  for (auto& e : linkCache_)
    if (e.first == target) return e.second;
  bool r = !storage::resolveLink(target, path_).empty();
  if (linkCache_.size() > 64) linkCache_.erase(linkCache_.begin());
  linkCache_.push_back({target, r});
  return r;
}

void EditorScreen::replace(uint32_t a, uint32_t b, const std::string& s, bool record) {
  if (readOnly_) return;
  if (text_.size() - (b - a) + s.size() > kMaxBytes) {
    app::toast("Note is too large");
    return;
  }
  std::string removed = text_.substr(a, b - a);
  if (record) {
    Op op{a, removed, s, cursor_, (uint32_t)(a + s.size()), millis()};
    bool merged = false;
    if (!undo_.empty()) {
      Op& last = undo_.back();
      bool recent = op.time - last.time < 2000;
      if (recent && removed.empty() && s.size() == 1 && s[0] != '\n' && last.removed.empty() &&
          last.pos + last.inserted.size() == a && !last.inserted.empty() &&
          !(s[0] == ' ' && last.inserted.back() != ' ')) {
        last.inserted += s;
        last.cursorAfter = op.cursorAfter;
        last.time = op.time;
        merged = true;
      } else if (recent && s.empty() && removed.size() <= 4 && removed.find('\n') == std::string::npos &&
                 last.inserted.empty() && last.pos == b) {
        last.removed = removed + last.removed;
        last.pos = a;
        last.cursorAfter = a;
        last.time = op.time;
        merged = true;
      }
    }
    if (!merged) {
      undo_.push_back(op);
      if (undo_.size() > 200) undo_.erase(undo_.begin());
    }
    redo_.clear();
  }

  int L = lineOf(a);
  int nlR = std::count(removed.begin(), removed.end(), '\n');
  // Grow in small steps: std::string would double its capacity (a 96 KB note would
  // briefly need ~290 KB of heap).
  size_t need = text_.size() - (b - a) + s.size();
  if (need > text_.capacity()) text_.reserve(need + 4096);
  text_.replace(a, b - a, s);
  lines_.erase(lines_.begin() + L + 1, lines_.begin() + L + 1 + nlR);
  std::vector<Line> fresh;
  for (size_t j = 0; j < s.size(); j++)
    if (s[j] == '\n') fresh.push_back({(uint32_t)(a + j + 1), 0, 0, true});
  lines_.insert(lines_.begin() + L + 1, fresh.begin(), fresh.end());
  int32_t delta = (int32_t)s.size() - (int32_t)removed.size();
  for (size_t k = L + 1 + fresh.size(); k < lines_.size(); k++) lines_[k].start += delta;
  for (size_t k = L; k <= L + fresh.size(); k++) lines_[k].dirty = true;

  auto shift = [&](uint32_t& p) {
    if (p >= b) p = p + delta;
    else if (p > a) p = a;
  };
  shift(cursor_);
  shift(anchor_);
  dirty_ = true;
  lastEdit_ = millis();
}

// ===========================================================================
// Layout

void EditorScreen::layoutLine(int i, bool revealed, Layout& L) {
  L.line = i;
  L.revealed = revealed;
  L.block = BL_TEXT;
  L.heading = L.quote = L.callout = 0;
  L.segs.clear();
  L.rows.clear();
  L.rowEnd.clear();
  L.alt.clear();
  L.media.clear();

  const char* p = text_.data() + lines_[i].start;
  uint32_t n = lineLen(i);
  const uint8_t st = lines_[i].stateIn;
  const int M = theme::MARGIN;
  uint32_t fw = 0;
  while (fw < n && (p[fw] == ' ' || p[fw] == '\t')) fw++;
  uint32_t contentStart = 0;

  // A line that is only an embed (![[Note]] or an image) becomes a card / picture
  if (!revealed && !(st & (SS_FRONT | SS_CODE | SS_COMMENT)) && embedLine(p + fw, n - fw, L)) {
    if (L.block == BL_IMAGE) return;
    p = L.alt.data();  // lay out the card's text instead of the source line
    n = L.alt.size();
  } else
  // Fence lines (``` and the --- around frontmatter) are syntax: hidden unless revealed.
  if ((st & SS_COMMENT) || (!(st & (SS_FRONT | SS_CODE)) && startsWith(p, n, fw, "%%") &&
                            !memmem(p + fw + 2, n - fw - 2, "%%", 2))) {
    L.block = BL_COMMENT;  // %% comment block: hidden unless the cursor is on it
    if (n) L.segs.push_back({0, n, ST_FAINT, KD_MARK});
  } else if (st & SS_FRONT) {
    bool end = n == 3 && memcmp(p, "---", 3) == 0;
    L.block = end ? BL_FENCE : BL_FRONT;
    if (n) L.segs.push_back({0, n, (uint16_t)(end ? ST_MONO | ST_FAINT : ST_MUTED), end ? KD_MARK : KD_TEXT});
  } else if (st & SS_CODE) {
    bool end = stateOut(st, i) == 0;
    L.block = end ? BL_FENCE : BL_CODE;
    if (n) L.segs.push_back({0, n, (uint16_t)(end ? ST_MONO | ST_FAINT : ST_MONO), end ? KD_MARK : KD_TEXT});
  } else if ((i == 0 && n == 3 && memcmp(p, "---", 3) == 0) || startsWith(p, n, fw, "```") ||
             startsWith(p, n, fw, "~~~")) {
    L.block = BL_FENCE;
    L.segs.push_back({0, n, ST_MONO | ST_FAINT, KD_MARK});
  } else {
    uint32_t pos = fw;
    if (fw > 0) L.segs.push_back({0, fw, 0, KD_TEXT});
    uint32_t qs = pos;
    while (pos < n && p[pos] == '>') {
      L.quote++;
      pos++;
      if (pos < n && p[pos] == ' ') pos++;
    }
    if (L.quote) L.segs.push_back({qs, pos, 0, KD_MARK});
    uint16_t base = 0;
    if (L.quote) {
      int idx;
      uint32_t ta, tb, he;
      if (parseCallout(p, n, pos, &idx, &ta, &tb, &he)) {
        // Title line: the [!type] marker is syntax; the title (or, without one, the type
        // name) shows bold in the callout colour
        L.callout = idx;
        base = ST_CALLOUT | ST_BOLD;
        if (he < n) {
          L.segs.push_back({pos, he, ST_CALLOUT, KD_MARK});
        } else {
          L.segs.push_back({pos, ta, ST_CALLOUT, KD_MARK});
          L.segs.push_back({ta, tb, (uint16_t)(ST_CALLOUT | ST_BOLD), KD_TEXT});
          L.segs.push_back({tb, n, ST_CALLOUT, KD_MARK});
        }
        pos = he;
      } else {
        L.callout = (st >> kCalloutShift) & 7;
      }
    }
    uint32_t h = 0;
    while (pos + h < n && p[pos + h] == '#') h++;
    if (h >= 1 && h <= 6 && pos + h < n && p[pos + h] == ' ') {
      L.heading = h;
      L.segs.push_back({pos, pos + h + 1, 0, KD_MARK});
      pos += h + 1;
    } else if (isHr(p + pos, n - pos)) {
      L.segs.push_back({pos, n, ST_FAINT, (uint8_t)(revealed ? KD_TEXT : KD_RULE)});
      pos = n;
    } else if (pos + 1 < n && strchr("-*+", p[pos]) && p[pos + 1] == ' ') {
      L.segs.push_back({pos, pos + 2, ST_FAINT, (uint8_t)(revealed ? KD_TEXT : KD_BULLET)});
      pos += 2;
      if (pos + 3 <= n && p[pos] == '[' && p[pos + 2] == ']' && strchr(" xX", p[pos + 1]) &&
          (pos + 3 == n || p[pos + 3] == ' ')) {
        bool done = p[pos + 1] != ' ';
        uint32_t e = pos + 3 < n ? pos + 4 : pos + 3;
        L.segs.push_back({pos, e, ST_FAINT,
                          (uint8_t)(revealed ? KD_TEXT : done ? KD_CHECK_DONE : KD_CHECK)});
        pos = e;
        if (done) base = ST_STRIKE | ST_MUTED;
      }
    } else {
      uint32_t d = pos;
      while (d < n && d - pos < 9 && isdigit((unsigned char)p[d])) d++;
      if (d > pos && d + 1 < n && (p[d] == '.' || p[d] == ')') && p[d + 1] == ' ') {
        L.segs.push_back({pos, d + 2, ST_MUTED, KD_TEXT});
        pos = d + 2;
      }
    }
    contentStart = pos;
    inlineSegs(p, pos, n, base, [this](const std::string& t) { return resolved(t); }, L.segs);
  }

  // --- Glyph widths
  std::vector<uint8_t> w(n, 0);
  for (auto& sg : L.segs) {
    if ((!revealed && sg.kind == KD_MARK) || sg.kind == KD_RULE) continue;
    if (sg.kind == KD_BULLET) {
      w[sg.a] = 18;
      continue;
    }
    if (sg.kind == KD_CHECK || sg.kind == KD_CHECK_DONE) {
      w[sg.a] = 24;
      continue;
    }
    uint8_t f = fontFor(sg.style, L.heading);
    for (uint32_t k = sg.a; k < sg.b;) {
      uint32_t cp;
      int len = tf::decodeUtf8(p, n, k, &cp);
      if (cp == '\t') w[k] = kTabW;
      else if (cp == '\n') w[k] = 0;
      else if (cp < 0x80) w[k] = tf::advance(f, (char)cp);
      else if (int adv = tf::glyphAdvance(f, cp); adv >= 0) w[k] = adv;
      else {
        const char* a = tf::asciiFor(cp);
        w[k] = tf::width(f, a, strlen(a));
      }
      k += len;
    }
  }

  // --- Wrap into rows
  const bool boxed = L.block != BL_TEXT;
  const int x0 = L.block == BL_EMBED ? M + 14 : boxed ? M + 8 : M + (revealed ? 0 : L.quote * kQuoteW);
  const int right = gfx.width() - M - (boxed ? 8 : 4);
  int hang = x0;
  if (!boxed)
    for (uint32_t k = 0; k < contentStart; k++) hang += w[k];
  hang = std::min(hang, right - 120);

  L.x.assign(n + 1, 0);
  L.row.assign(n + 1, 0);
  int x = x0, r = 0;
  uint32_t rowStart = 0;
  int64_t lastBreak = -1;
  for (uint32_t k = 0; k < n; k++) {
    if (p[k] == '\n' && r < 250) {  // embed card text: one row per source line
      L.x[k] = x;
      L.row[k] = r;
      L.rows.push_back({rowStart, k + 1, 0, 0, 0});
      L.rowEnd.push_back(x);
      r++;
      rowStart = k + 1;
      x = x0;
      lastBreak = -1;
      continue;
    }
    if (w[k] > 0 && x + w[k] > right && k > rowStart && r < 250) {
      uint32_t brk = (lastBreak > (int64_t)rowStart && lastBreak <= (int64_t)k) ? lastBreak : k;
      L.rows.push_back({rowStart, brk, 0, 0, 0});
      L.rowEnd.push_back(brk < k ? L.x[brk] : x);
      r++;
      rowStart = brk;
      x = hang;
      for (uint32_t j = brk; j < k; j++) {
        L.x[j] = x;
        L.row[j] = r;
        x += w[j];
      }
      lastBreak = -1;
    }
    L.x[k] = x;
    L.row[k] = r;
    x += w[k];
    if (p[k] == ' ' || p[k] == '\t') lastBreak = k + 1;
  }
  L.x[n] = x;
  L.row[n] = r;
  L.rows.push_back({rowStart, n, 0, 0, 0});
  L.rowEnd.push_back(x);

  // --- Row metrics
  const auto& body = tf::get(tf::BODY);
  int y = L.heading == 1 || L.heading == 2 ? 10 : L.heading ? 5 : 0;
  for (size_t ri = 0; ri < L.rows.size(); ri++) {
    Row& row = L.rows[ri];
    int asc = body.ascent, desc = body.descent;
    for (auto& sg : L.segs) {
      if (sg.b <= row.a || sg.a >= row.b) continue;
      if (!revealed && sg.kind == KD_MARK) continue;
      const auto& fi = tf::get(fontFor(sg.style, L.heading));
      asc = std::max<int>(asc, fi.ascent);
      desc = std::max<int>(desc, fi.descent);
    }
    row.top = y;
    row.asc = asc;
    row.h = asc + desc + kGap;
    y += row.h;
  }
  L.height = y + (L.heading ? 2 : 0);
  if (L.block == BL_EMBED) {  // padding inside the card
    for (auto& row : L.rows) row.top += 6;
    L.height += 14;
  }
  if (!revealed && L.block == BL_COMMENT) {  // hidden comment line: (almost) no space
    L.rows.resize(1);
    L.rows[0] = {0, n, 0, 2, 3};
    L.height = 3;
  }
  if (!revealed && L.block == BL_FENCE) {  // hidden fence: just a thin strip of the code box
    L.rows.resize(1);
    L.rows[0] = {0, n, 0, 8, 10};
    L.height = 10;
  }
}

// Recognises a whole-line embed: ![[target|opt]] or ![alt](target). For images it sets up
// an image block; for notes it builds the card text in L.alt. Returns false if the line
// isn't an embed (or the target can't be found).
bool EditorScreen::embedLine(const char* p, uint32_t n, Layout& L) {
  while (n && (p[n - 1] == ' ' || p[n - 1] == '\t')) n--;
  std::string target, opt;
  if (n > 5 && memcmp(p, "![[", 3) == 0 && memcmp(p + n - 2, "]]", 2) == 0) {
    std::string inner(p + 3, n - 5);
    if (inner.find("]]") != std::string::npos) return false;
    size_t bar = inner.find('|');
    target = inner.substr(0, bar);
    if (bar != std::string::npos) opt = inner.substr(bar + 1);
  } else if (n > 5 && p[0] == '!' && p[1] == '[' && p[n - 1] == ')') {
    std::string s(p, n);
    size_t mid = s.find("](");
    if (mid == std::string::npos || s.find(')') != n - 1) return false;
    target = s.substr(mid + 2, n - mid - 3);
    if (target.find("://") != std::string::npos) return false;
    std::string dec;  // %20 etc.
    for (size_t i = 0; i < target.size(); i++) {
      if (target[i] == '%' && i + 2 < target.size()) {
        dec += (char)strtol(target.substr(i + 1, 2).c_str(), nullptr, 16);
        i += 2;
      } else {
        dec += target[i];
      }
    }
    target = dec;
  } else {
    return false;
  }
  const int W = gfx.width(), M = theme::MARGIN;

  if (storage::isImageName(target)) {
    std::string path = storage::resolveAttachment(target, path_);
    if (path.empty()) return false;
    auto it = imgSizes_.find(path);
    if (it == imgSizes_.end()) {
      int w = 0, h = 0;
      if (!storage::imageSize(path, &w, &h)) w = h = 0;
      it = imgSizes_.emplace(path, std::make_pair(w, h)).first;
    }
    const int iw = it->second.first, ih = it->second.second;
    if (iw <= 0 || ih <= 0) return false;
    // Obsidian's ![[img.png|300]] sets the width
    float want = std::min<float>(iw, W - 2 * M);
    if (atoi(opt.c_str()) > 0) want = std::min<float>(want, atoi(opt.c_str()));
    float scale = want / iw;
    if (ih * scale > kImageMaxH) scale = (float)kImageMaxH / ih;
    L.block = BL_IMAGE;
    L.media = path;
    L.imgScale = scale;
    L.imgW = std::max(1, (int)(iw * scale));
    L.imgH = std::max(1, (int)(ih * scale));
    L.segs.push_back({0, n, 0, KD_MARK});
    L.x.assign(n + 1, M);
    L.row.assign(n + 1, 0);
    L.rows.push_back({0, n, 0, 0, (int16_t)(L.imgH + 10)});
    L.rowEnd.push_back(M);
    L.height = L.imgH + 10;
    return true;
  }

  // Note embed: show the start of the note (or of the #heading section)
  std::string heading;
  size_t hash = target.find('#');
  if (hash != std::string::npos) {
    heading = target.substr(hash + 1);
    target = target.substr(0, hash);
  }
  std::string path = target.empty() ? path_ : storage::resolveLink(target, path_);
  if (path.empty()) return false;
  const std::string key = path + "#" + heading;
  auto it = embeds_.find(key);
  if (it == embeds_.end()) {
    std::string text;
    storage::readFile(path, text, 16 * 1024);
    std::string title = storage::baseName(path) + (heading.empty() ? "" : " > " + heading);
    std::string body;
    int lines = 0, level = 0;
    bool inSection = heading.empty(), front = text.compare(0, 4, "---\n") == 0, more = false;
    size_t i = 0;
    for (int ln = 0; i <= text.size(); ln++) {
      size_t e = text.find('\n', i);
      if (e == std::string::npos) e = text.size();
      std::string line = text.substr(i, e - i);
      i = e + 1;
      if (front) {
        if (ln > 0 && line == "---") front = false;
        continue;
      }
      size_t h = 0;
      while (h < line.size() && line[h] == '#') h++;
      bool isHeading = h >= 1 && h <= 6 && h < line.size() && line[h] == ' ';
      if (!heading.empty()) {
        if (!inSection) {
          if (isHeading && strcasecmp(line.substr(h + 1).c_str(), heading.c_str()) == 0) inSection = true, level = h;
          continue;
        }
        if (isHeading && (int)h <= level) break;  // next section at the same level
      }
      std::string plain = tf::plainLine(line);
      if (plain.empty()) continue;
      if (lines == 10) {
        more = true;
        break;
      }
      if (plain.size() > 300) plain = plain.substr(0, 300) + "...";
      body += "\n" + plain;
      lines++;
    }
    if (more) body += "\n...";
    it = embeds_.emplace(key, title + body).first;
  }
  L.alt = it->second;
  L.media = path;
  L.block = BL_EMBED;
  size_t nl = L.alt.find('\n');
  uint32_t titleEnd = nl == std::string::npos ? L.alt.size() : nl;
  L.segs.push_back({0, titleEnd, (uint16_t)(ST_LINK | ST_BOLD), KD_TEXT});
  if (titleEnd < L.alt.size()) L.segs.push_back({titleEnd, (uint32_t)L.alt.size(), 0, KD_TEXT});
  return true;
}

// Images are decoded straight to the panel after the text (bands) have been drawn
void EditorScreen::drawImages(int regionY, int regionH) {
  if (fastScroll_ || popup_) return;
  const int M = theme::MARGIN;
  for (auto& L : visible_) {
    if (L.block != BL_IMAGE) continue;
    const int y = tops_[L.line] - scroll_ + 5;  // content coordinates
    if (y + L.imgH <= regionY || y >= regionY + regionH) continue;
    const int top = std::max(regionY, std::max(0, y)), bot = std::min({regionY + regionH, viewH(), y + L.imgH});
    if (bot <= top) continue;
    gfx.setClipRect(0, theme::BAR_H + top, gfx.width(), bot - top);
    if (!storage::drawImage(gfx, L.media, M, theme::BAR_H + y, L.imgScale))
      Serial.printf("[editor] couldn't draw %s\n", L.media.c_str());
    gfx.clearClipRect();
  }
  // LovyanGFX keeps its ~45 KB PNG decoder for reuse; we can't spare that much RAM
  gfx.releasePngMemory();
}

// ===========================================================================
// Drawing

// The status bar gives way to the on-screen keyboard when that is up.
int EditorScreen::viewH() const {
  return ui::contentBottom() - theme::BAR_H - (osk::visible() ? 0 : kStatusH);
}

int EditorScreen::lineAtY(int32_t y) const {
  int lo = 0, hi = tops_.size() - 1;
  while (lo < hi) {
    int mid = (lo + hi + 1) / 2;
    if (tops_[mid] <= y) lo = mid;
    else hi = mid - 1;
  }
  return lo;
}

void EditorScreen::layoutVisible() {
  visible_.clear();
  if (lines_.empty()) return;
  for (int i = lineAtY(scroll_); i < (int)lines_.size() && tops_[i] < scroll_ + viewH(); i++) {
    visible_.emplace_back();
    layoutLine(i, revealedLine(i), visible_.back());
  }
}

void EditorScreen::drawLine(LGFX_Sprite& s, const Layout& L, int y0) {
  const bool alt = !L.alt.empty();
  const char* p = alt ? L.alt.data() : text_.data() + lines_[L.line].start;
  const uint32_t n = alt ? L.alt.size() : lineLen(L.line);
  const uint32_t ls = lines_[L.line].start;
  const int M = theme::MARGIN, W = gfx.width();

  if (L.block == BL_IMAGE) {  // placeholder; the picture itself is drawn over it later
    s.fillRoundRect(M, y0 + 5, L.imgW, L.imgH, 4, theme::BG_ALT);
    s.setFont(font::small());
    s.setTextColor(theme::FAINT);
    s.setTextDatum(textdatum_t::middle_center);
    s.drawString(tf::toAscii(storage::baseName(L.media + ".md")).c_str(), M + L.imgW / 2, y0 + 5 + L.imgH / 2);
    s.setTextDatum(textdatum_t::top_left);
    return;
  }
  if (L.block == BL_EMBED) {  // embedded note card
    s.fillRoundRect(M, y0 + 2, W - 2 * M, L.height - 4, 6, tint(theme::ACCENT, 30));
    s.fillRect(M, y0 + 2, 3, L.height - 4, theme::ACCENT);
  } else if (L.block != BL_TEXT && L.block != BL_COMMENT) {
    s.fillRect(M, y0, W - 2 * M, L.height, theme::CODE_BG);
  }
  if (L.callout) s.fillRect(M, y0, W - 2 * M, L.height, tint(calloutColor(L.callout), 36));
  if (!L.revealed)
    for (int q = 0; q < L.quote; q++)
      s.fillRect(M + q * kQuoteW, y0, 3, L.height, L.callout ? calloutColor(L.callout) : theme::QUOTE_BAR);

  // Selection
  if (hasSelection() && !alt) {
    uint32_t sa = std::min(anchor_, cursor_), sb = std::max(anchor_, cursor_);
    if (sa <= ls + n && sb >= ls) {
      uint32_t la = sa > ls ? sa - ls : 0, lb = std::min<uint32_t>(sb - ls, n);
      for (size_t ri = 0; ri < L.rows.size(); ri++) {
        const Row& row = L.rows[ri];
        uint32_t a = std::max(la, row.a), b = std::min(lb, row.b);
        bool past = sb > ls + row.b;  // selection continues beyond this row
        if (a > b || (a == b && !past)) continue;
        int x1 = L.x[a];
        int x2 = (b < row.b || ri + 1 == L.rows.size()) ? (b < row.b ? L.x[b] : L.rowEnd[ri]) : L.rowEnd[ri];
        if (past) x2 += 6;
        s.fillRect(x1, y0 + row.top, std::max(2, x2 - x1), row.h, theme::ACCENT_BG);
      }
    }
  }

  char buf[160];
  for (auto& sg : L.segs) {
    if (!L.revealed && sg.kind == KD_MARK) continue;
    if (sg.b <= sg.a && sg.kind != KD_RULE) continue;
    if (sg.kind == KD_RULE) {
      s.fillRect(M, y0 + L.height / 2, W - 2 * M, 1, theme::BORDER);
      continue;
    }
    const Row& row = L.rows[L.row[sg.a]];
    const int base = y0 + row.top + row.asc;
    if (sg.kind == KD_BULLET) {
      s.fillCircle(L.x[sg.a] + 6, base - tf::get(tf::BODY).ascent / 2 + 1, 3, theme::MUTED);
      continue;
    }
    if (sg.kind == KD_CHECK || sg.kind == KD_CHECK_DONE) {
      int bx = L.x[sg.a] + 2, by = base - 13;
      if (sg.kind == KD_CHECK) {
        s.drawRoundRect(bx, by, 14, 14, 3, theme::MUTED);
      } else {
        s.fillRoundRect(bx, by, 14, 14, 3, theme::ACCENT);
        s.drawLine(bx + 3, by + 7, bx + 6, by + 10, theme::BG);
        s.drawLine(bx + 6, by + 10, bx + 11, by + 4, theme::BG);
        s.drawLine(bx + 3, by + 8, bx + 6, by + 11, theme::BG);
        s.drawLine(bx + 6, by + 11, bx + 11, by + 5, theme::BG);
      }
      continue;
    }

    const uint8_t f = fontFor(sg.style, L.heading);
    const Colors col = colorsFor(sg, L.heading, calloutColor(L.callout));
    s.setFont(tf::get(f).font);
    s.setTextColor(col.fg);
    s.setTextDatum(textdatum_t::baseline_left);
    uint32_t k = sg.a;
    while (k < sg.b) {
      const int r = L.row[k];
      const Row& rw = L.rows[r];
      const int bl = y0 + rw.top + rw.asc;
      uint32_t rs = k;
      size_t bl_n = 0;
      int width = 0;
      while (k < sg.b && L.row[k] == r) {
        uint32_t cp;
        int len = tf::decodeUtf8(p, n, k, &cp);
        if (cp == '\t' || cp == '\n') {
          k += len;
          break;
        }
        const int ga = cp < 0x80 ? -1 : tf::glyphAdvance(f, cp);
        const char* g = (cp < 0x80 || ga >= 0) ? nullptr : tf::asciiFor(cp);
        if (ga >= 0) {  // the font has it: copy the UTF-8 bytes as they are
          if (bl_n + len < sizeof(buf) - 1) memcpy(buf + bl_n, p + k, len), bl_n += len;
          width += ga;
        } else if (g) {
          size_t gl = strlen(g);
          if (bl_n + gl < sizeof(buf) - 1) memcpy(buf + bl_n, g, gl), bl_n += gl;
          width += tf::width(f, g, gl);
        } else if (bl_n < sizeof(buf) - 1) {
          buf[bl_n++] = (char)cp;
          width += tf::advance(f, (char)cp);
        }
        k += len;
        if (bl_n >= sizeof(buf) - 5) break;
      }
      buf[bl_n] = 0;
      if (col.hasBg && width > 0) s.fillRoundRect(L.x[rs] - 2, y0 + rw.top, width + 4, rw.h - kGap + 2, 3, col.bg);
      if (bl_n) s.drawString(buf, L.x[rs], bl);
      if ((sg.style & ST_STRIKE) && width > 0)
        s.drawFastHLine(L.x[rs], bl - tf::get(f).ascent / 3, width, col.fg);
    }
  }

  // Cursor
  if (!reading_ && L.line == cursorLine_ && !hasSelection() && !alt) {
    uint32_t c = cursor_ - ls;
    const Row& row = L.rows[L.row[c]];
    s.fillRect(L.x[c] - 1, y0 + row.top, 2, row.h - kGap + 2, theme::ACCENT);
  }
}

void EditorScreen::placePopup() {
  if (!popup_) return;
  Layout L;
  layoutLine(cursorLine_, true, L);
  uint32_t c = cursor_ - lines_[cursorLine_].start;
  const Row& row = L.rows[L.row[c]];
  const int rowTop = tops_[cursorLine_] + row.top, rowBot = rowTop + row.h;
  // Open on whichever side of the cursor line has room, showing as many items as fit.
  const int n = popupItems_.size();
  const int full = n * 28 + 8;
  const int below = scroll_ + viewH() - rowBot - 4, above = rowTop - scroll_ - 4;
  const bool down = below >= full || below >= above;
  popupRows_ = std::max(1, std::min(n, ((down ? below : above) - 8) / 28));
  popupFirst_ = std::max(0, std::min(popupFirst_, n - popupRows_));
  if (popupSel_ < popupFirst_) popupFirst_ = popupSel_;
  if (popupSel_ >= popupFirst_ + popupRows_) popupFirst_ = popupSel_ - popupRows_ + 1;
  popupW_ = 300;
  popupH_ = popupRows_ * 28 + 8;
  popupX_ = std::max(theme::MARGIN, std::min<int>(L.x[c] - 10, gfx.width() - popupW_ - 8));
  popupY_ = down ? rowBot + 2 : rowTop - popupH_ - 2;
}

void EditorScreen::drawPopup(LGFX_Sprite& s, int bandTop) {
  if (!popup_) return;
  int y = popupY_ - scroll_ - bandTop;
  if (y > s.height() || y + popupH_ < 0) return;
  s.fillRoundRect(popupX_ - 1, y - 1, popupW_ + 2, popupH_ + 2, 7, theme::BORDER);
  s.fillRoundRect(popupX_, y, popupW_, popupH_, 6, theme::BG_ALT);
  const int n = popupItems_.size();
  if (popupRows_ < n) {  // scrollbar when some suggestions are out of view
    int track = popupH_ - 12, thumb = std::max(10, track * popupRows_ / n);
    int ty = y + 6 + (track - thumb) * popupFirst_ / std::max(1, n - popupRows_);
    s.fillRoundRect(popupX_ + popupW_ - 5, ty, 3, thumb, 1, theme::MUTED);
  }
  for (int i = popupFirst_; i < popupFirst_ + popupRows_ && i < n; i++) {
    int iy = y + 4 + (i - popupFirst_) * 28;
    if (i == popupSel_) s.fillRoundRect(popupX_ + 4, iy, popupW_ - 12, 28, 4, theme::ACCENT_BG);
    const std::string& path = popupItems_[i];
    std::string folder = storage::parentDir(path);
    s.setFont(font::ui());
    s.setTextDatum(textdatum_t::middle_left);
    s.setTextColor(theme::TEXT_BRIGHT);
    s.drawString(ui::ellipsize(storage::baseName(path), popupW_ - 110, font::ui()).c_str(),
                 popupX_ + 12, iy + 14);
    if (folder != "/") {
      s.setFont(font::small());
      s.setTextColor(theme::MUTED);
      s.setTextDatum(textdatum_t::middle_right);
      s.drawString(ui::ellipsize(folder.substr(1), 90, font::small()).c_str(), popupX_ + popupW_ - 12,
                   iy + 14);
    }
  }
}

void EditorScreen::drawRegion(int y, int h) {
  if (h <= 0) return;
  const int W = gfx.width();
  const int vh = viewH();
  const int maxScroll = std::max(0, totalH_ - vh);
  renderBands(theme::BAR_H + y, h, theme::BG, [&](LGFX_Sprite& s, int off) {
    const int bandTop = y + off;
    for (auto& L : visible_) {
      int y0 = tops_[L.line] - scroll_ - bandTop;
      if (y0 > s.height() || y0 + L.height < 0) continue;
      drawLine(s, L, y0);
    }
    drawPopup(s, bandTop);
    if (maxScroll > 0) {
      int thumb = std::max(24, vh * vh / (vh + maxScroll));
      int ty = (vh - thumb) * scroll_ / maxScroll - bandTop;
      s.fillRoundRect(W - 4, ty, 3, thumb, 1, theme::BORDER);
    }
  });
  drawImages(y, h);
}

void EditorScreen::drawContent() {
  uint32_t t0 = micros();
  layoutVisible();
  uint32_t t1 = micros();
  placePopup();
  drawRegion(0, viewH());
  if (micros() - t0 > 60000)
    Serial.printf("[editor] slow redraw: layout %u us, render %u us\n", (unsigned)(t1 - t0), (unsigned)(micros() - t1));
}

void EditorScreen::drawLineOnly(int line) {
  if (line < 0 || line >= (int)lines_.size()) return;
  bool found = false;
  for (auto& L : visible_) {
    if (L.line == line) {
      layoutLine(line, revealedLine(line), L);
      found = true;
    }
  }
  if (!found) return;  // off screen
  int y = tops_[line] - scroll_;
  int h = lines_[line].height;
  int top = std::max(0, y), bot = std::min(viewH(), y + h);
  drawRegion(top, bot - top);
}

void EditorScreen::drawStatus() {
  if (osk::visible()) return;
  const int y = gfx.height() - kStatusH, W = gfx.width();
  gfx.fillRect(0, y, W, kStatusH, theme::BAR);
  gfx.setFont(font::small());
  gfx.setTextDatum(textdatum_t::middle_left);
  gfx.setTextColor(theme::MUTED);
  if (reading_) {
    gfx.drawString("Reading view", theme::MARGIN, y + kStatusH / 2);
    gfx.setTextDatum(textdatum_t::middle_right);
    char buf[32];
    snprintf(buf, sizeof(buf), "%u words", (unsigned)words_);
    gfx.drawString(buf, W - theme::MARGIN, y + kStatusH / 2);
    gfx.setTextDatum(textdatum_t::top_left);
    return;
  }
  uint32_t col = 1;
  for (uint32_t k = lines_[cursorLine_].start; k < cursor_; k++)
    if (((uint8_t)text_[k] & 0xC0) != 0x80) col++;
  char buf[64];
  snprintf(buf, sizeof(buf), "Ln %d, Col %u", cursorLine_ + 1, (unsigned)col);
  gfx.drawString(buf, theme::MARGIN, y + kStatusH / 2);
  gfx.setTextDatum(textdatum_t::middle_right);
  const char* state = readOnly_ ? "Read only (too large)" : dirty_ ? "Edited" : "Saved";
  gfx.setTextColor(dirty_ ? theme::MUTED : theme::FAINT);
  gfx.drawString(state, W - theme::MARGIN, y + kStatusH / 2);
  gfx.setTextDatum(textdatum_t::top_left);
}

void EditorScreen::drawTitle() {
  ui::topBar(tf::printable(storage::baseName(path_)) + (dirty_ ? " *" : ""), ui::Icon::Back,
             ui::Icon::More, reading_ ? ui::Icon::Pencil : ui::Icon::Eye,
             reading_ ? ui::Icon::None : ui::Icon::Keyboard);
  titleDirty_ = dirty_;
}

void EditorScreen::draw() {
  if (!reading_) ensureCursorVisible();  // the view may have shrunk for the keyboard
  drawTitle();
  drawContent();
  drawStatus();
  if (!loadError_.empty()) {
    app::toast(loadError_, 3000);
    loadError_.clear();
  }
}

// ===========================================================================
// Opening / saving

bool EditorScreen::open(const std::string& path, int scroll, bool reading) {
  path_ = path;
  reading_ = reading;
  imgSizes_.clear();
  embeds_.clear();
  text_ = std::string();  // release the previous note's buffer before allocating the next
  undo_.clear();
  redo_.clear();
  visible_.clear();
  cursor_ = anchor_ = 0;
  cursorLine_ = 0;
  goalX_ = -1;
  dirty_ = false;
  popup_ = false;
  bool ok = false;
  try {
    int64_t size = storage::fileSize(path);
    readOnly_ = size > (int64_t)kMaxBytes;
    if (readOnly_) loadError_ = "Note is over 96 KB: showing the start, read only";
    if (size > 0) text_.reserve(std::min<int64_t>(size, kMaxBytes) + 2048);
    ok = storage::readFile(path, text_, kMaxBytes);
    text_.erase(std::remove(text_.begin(), text_.end(), '\r'), text_.end());
    rebuildLines();
    relayout();
  } catch (const std::bad_alloc&) {
    text_ = std::string();
    rebuildLines();
    relayout();
    readOnly_ = true;
    ok = false;
    loadError_ = "Not enough memory to open this note";
  }
  if (!ok && loadError_.empty()) loadError_ = "Couldn't read note";

  words_ = 0;
  bool inWord = false;
  for (char c : text_) {
    bool w = !isspace((unsigned char)c);
    if (w && !inWord) words_++;
    inWord = w;
  }

  scroll_ = std::max(0, std::min(scroll, totalH_ - viewH()));
  if (!reading_ && scroll_ > 0) {
    // Put the cursor on the first fully visible line
    int line = std::min<int>(lineAtY(scroll_ + 8) + 1, lines_.size() - 1);
    cursor_ = anchor_ = lines_[line].start;
    cursorLine_ = line;
    lines_[0].dirty = true;
    lines_[line].dirty = true;
    relayout();
  }
  Serial.printf("[note] %s %s: %u bytes, %u lines, heap %u (largest %u)\n", reading ? "read" : "edit",
                path.c_str(), (unsigned)text_.size(), (unsigned)lines_.size(), ESP.getFreeHeap(),
                ESP.getMaxAllocHeap());
  return ok;
}

bool EditorScreen::save() {
  if (!dirty_ || readOnly_) return true;
  bool ok = storage::writeFile(path_, text_);
  Serial.printf("[editor] save %s: %s\n", path_.c_str(), ok ? "ok" : "FAILED");
  if (ok) {
    dirty_ = false;
  } else {
    app::toast("Save failed!", 3000);
  }
  return ok;
}

void EditorScreen::onLeave() { save(); }

void EditorScreen::revealLine(int line) {
  if (lines_.empty()) return;
  line = std::max(0, std::min<int>(line, lines_.size() - 1));
  if (!reading_) {
    lines_[cursorLine_].dirty = true;
    cursor_ = anchor_ = lines_[line].start;
    cursorLine_ = line;
    lines_[line].dirty = true;
    relayout();
  }
  scroll_ = std::max(0, std::min<int>(tops_[line] - 30, totalH_ - viewH()));
  draw();
}

void EditorScreen::setCursorPos(int pos) {
  if (reading_ || pos < 0) return;
  lines_[cursorLine_].dirty = true;
  cursor_ = anchor_ = std::min<uint32_t>(pos, text_.size());
  while (cursor_ > 0 && ((uint8_t)text_[cursor_] & 0xC0) == 0x80) cursor_--;  // stay on a char boundary
  anchor_ = cursor_;
  cursorLine_ = lineOf(cursor_);
  lines_[cursorLine_].dirty = true;
  relayout();
}

void EditorScreen::insertAtCursor(const std::string& s) {
  const int prevLine = cursorLine_;
  insertText(s);
  afterChange(prevLine, -1, true);
}

void EditorScreen::reloadIfClean() {
  if (dirty_) return;
  uint32_t keep = cursor_;
  open(path_, scroll_, reading_);
  if (!reading_ && keep <= text_.size()) {
    lines_[cursorLine_].dirty = true;
    cursor_ = anchor_ = keep;
    cursorLine_ = lineOf(cursor_);
    lines_[cursorLine_].dirty = true;
    relayout();
  }
  draw();
}

void EditorScreen::tick() {
  if (dirty_ && millis() - lastEdit_ > 2000) {
    save();
    if (!dirty_) {
      drawTitle();
      drawStatus();
    }
  }
}

// ===========================================================================
// Cursor movement

void EditorScreen::setCursor(uint32_t pos, bool extend) {
  cursor_ = std::min<uint32_t>(pos, text_.size());
  if (!extend) anchor_ = cursor_;
}

uint32_t EditorScreen::prevChar(uint32_t p) const {
  if (p == 0) return 0;
  p--;
  while (p > 0 && ((uint8_t)text_[p] & 0xC0) == 0x80) p--;
  return p;
}

uint32_t EditorScreen::nextChar(uint32_t p) const {
  if (p >= text_.size()) return text_.size();
  p++;
  while (p < text_.size() && ((uint8_t)text_[p] & 0xC0) == 0x80) p++;
  return p;
}

uint32_t EditorScreen::wordLeft(uint32_t p) const {
  while (p > 0 && !isWordChar(text_[p - 1])) p--;
  while (p > 0 && isWordChar(text_[p - 1])) p--;
  return p;
}

uint32_t EditorScreen::wordRight(uint32_t p) const {
  const uint32_t n = text_.size();
  while (p < n && !isWordChar(text_[p])) p++;
  while (p < n && isWordChar(text_[p])) p++;
  return p;
}

uint32_t EditorScreen::hitTestRow(const Layout& L, int r, int x) {
  const Row& row = L.rows[r];
  const bool last = r + 1 == (int)L.rows.size();
  uint32_t end = last ? row.b : (row.b > row.a ? row.b - 1 : row.b);
  uint32_t best = row.a;
  int bestD = 1 << 30;
  for (uint32_t k = row.a; k <= end; k++) {
    if (k < L.x.size() - 1 && k > row.a && L.row[k] != r) continue;
    int d = abs(L.x[k] - x);
    if (d < bestD) {
      bestD = d;
      best = k;
    }
  }
  return best;
}

uint32_t EditorScreen::hitTest(int line, int x, int y) {
  Layout L;
  layoutLine(line, revealedLine(line), L);
  int r = 0;
  while (r + 1 < (int)L.rows.size() && y >= L.rows[r].top + L.rows[r].h) r++;
  uint32_t k = hitTestRow(L, r, x);
  // Don't land inside a UTF-8 sequence
  const uint32_t ls = lines_[line].start;
  while (k > 0 && ls + k < text_.size() && ((uint8_t)text_[ls + k] & 0xC0) == 0x80) k--;
  return ls + k;
}

void EditorScreen::moveVertical(int dir, bool extend) {
  Layout L;
  layoutLine(cursorLine_, true, L);
  const uint32_t ls = lines_[cursorLine_].start;
  uint32_t c = cursor_ - ls;
  int r = L.row[c];
  if (goalX_ < 0) goalX_ = L.x[c];
  uint32_t target;
  if (dir < 0 && r > 0) {
    target = ls + hitTestRow(L, r - 1, goalX_);
  } else if (dir > 0 && r + 1 < (int)L.rows.size()) {
    target = ls + hitTestRow(L, r + 1, goalX_);
  } else {
    int nl = cursorLine_ + dir;
    if (nl < 0) target = 0;
    else if (nl >= (int)lines_.size()) target = text_.size();
    else {
      Layout L2;
      layoutLine(nl, true, L2);
      target = lines_[nl].start + hitTestRow(L2, dir < 0 ? L2.rows.size() - 1 : 0, goalX_);
    }
  }
  setCursor(target, extend);
}

bool EditorScreen::ensureCursorVisible() {
  Layout L;
  layoutLine(cursorLine_, true, L);
  uint32_t c = cursor_ - lines_[cursorLine_].start;
  const Row& row = L.rows[L.row[c]];
  int top = tops_[cursorLine_] + row.top, bot = top + row.h;
  int old = scroll_;
  const int vh = viewH();
  if (top < scroll_) scroll_ = top - 8;
  else if (bot > scroll_ + vh) scroll_ = bot - vh + 8;
  scroll_ = std::max(0, std::min(scroll_, std::max(0, totalH_ - vh)));
  return scroll_ != old;
}

// ===========================================================================
// Editing operations

void EditorScreen::deleteSelection() {
  if (!hasSelection()) return;
  uint32_t a = std::min(anchor_, cursor_), b = std::max(anchor_, cursor_);
  replace(a, b, "");
  setCursor(a, false);
}

void EditorScreen::insertText(const std::string& s) {
  uint32_t a = std::min(anchor_, cursor_), b = std::max(anchor_, cursor_);
  replace(a, b, s);
  setCursor(a + s.size(), false);
}

void EditorScreen::newline(bool plain) {
  deleteSelection();
  const int L = cursorLine_ = lineOf(cursor_);
  const uint32_t ls = lines_[L].start, n = lineLen(L);
  const char* p = text_.data() + ls;
  if (plain || lines_[L].stateIn & SS_FRONT) return insertText("\n");

  uint32_t pos = 0;
  while (pos < n && (p[pos] == ' ' || p[pos] == '\t')) pos++;
  const uint32_t indentEnd = pos;
  if (lines_[L].stateIn & SS_CODE) return insertText("\n" + std::string(p, indentEnd));

  while (pos < n && p[pos] == '>') {
    pos++;
    if (pos < n && p[pos] == ' ') pos++;
  }
  const uint32_t quoteEnd = pos;
  std::string marker;
  if (pos + 1 < n && strchr("-*+", p[pos]) && p[pos + 1] == ' ') {
    marker = std::string(p + pos, 2);
    pos += 2;
    if (pos + 3 <= n && p[pos] == '[' && p[pos + 2] == ']') {
      marker += "[ ] ";
      pos = std::min(n, pos + 4);
    }
  } else {
    uint32_t d = pos;
    while (d < n && isdigit((unsigned char)p[d])) d++;
    if (d > pos && d + 1 < n && (p[d] == '.' || p[d] == ')') && p[d + 1] == ' ') {
      marker = std::to_string(atoi(std::string(p + pos, d - pos).c_str()) + 1) + p[d] + " ";
      pos = d + 2;
    }
  }
  const bool quoted = quoteEnd > indentEnd;
  if (!quoted && marker.empty()) return insertText("\n");
  if (cursor_ - ls < pos) return insertText("\n");

  bool emptyItem = true;
  for (uint32_t k = pos; k < n; k++)
    if (p[k] != ' ') emptyItem = false;
  if (emptyItem) {
    // Enter on an empty item ends the list (or outdents a nested item), like Obsidian.
    if (!marker.empty() && indentEnd > 0) {
      uint32_t cut = p[0] == '\t' ? 1 : std::min<uint32_t>(indentEnd, 4);
      replace(ls, ls + cut, "");
      setCursor(cursor_, false);
    } else {
      replace(ls + quoteEnd, ls + n, "");
      if (marker.empty() && quoted) replace(ls, ls + quoteEnd, "");
      setCursor(ls + (marker.empty() ? 0 : quoteEnd), false);
    }
    return;
  }
  insertText("\n" + std::string(p, quoteEnd) + marker);
}

void EditorScreen::indentLines(bool outdent) {
  uint32_t sa = std::min(anchor_, cursor_), sb = std::max(anchor_, cursor_);
  int first = lineOf(sa), last = lineOf(sb);
  if (last > first && sb == lines_[last].start) last--;
  for (int i = last; i >= first; i--) {
    uint32_t ls = lines_[i].start;
    if (outdent) {
      uint32_t n = lineLen(i), cut = 0;
      if (n && text_[ls] == '\t') cut = 1;
      else
        while (cut < 4 && cut < n && text_[ls + cut] == ' ') cut++;
      if (cut) replace(ls, ls + cut, "");
    } else {
      replace(ls, ls, "\t");
    }
  }
}

void EditorScreen::wrapSelection(const char* m) {
  const std::string mk = m;
  if (!hasSelection()) {
    insertText(mk + mk);
    setCursor(cursor_ - mk.size(), false);
    return;
  }
  uint32_t a = std::min(anchor_, cursor_), b = std::max(anchor_, cursor_);
  replace(b, b, mk);
  replace(a, a, mk);
  anchor_ = a + mk.size();
  cursor_ = b + mk.size();
}

void EditorScreen::toggleCheckbox(int line) {
  const uint32_t ls = lines_[line].start, n = lineLen(line);
  const char* p = text_.data() + ls;
  uint32_t pos = 0;
  while (pos < n && (p[pos] == ' ' || p[pos] == '\t')) pos++;
  if (pos + 1 < n && strchr("-*+", p[pos]) && p[pos + 1] == ' ') {
    pos += 2;
    if (pos + 2 < n + 0 && p[pos] == '[' && p[pos + 2] == ']') {
      replace(ls + pos + 1, ls + pos + 2, p[pos + 1] == ' ' ? "x" : " ");
    } else {
      replace(ls + pos, ls + pos, "[ ] ");
    }
  } else {
    replace(ls + pos, ls + pos, "- [ ] ");
  }
}

bool EditorScreen::linkTargetAt(uint32_t pos, std::string& target, bool& external) {
  const int L = lineOf(pos);
  const uint32_t ls = lines_[L].start;
  const std::string line = text_.substr(ls, lineLen(L));
  const uint32_t c = pos - ls;
  external = false;
  for (size_t a = line.find("[["); a != std::string::npos; a = line.find("[[", a + 2)) {
    size_t b = line.find("]]", a + 2);
    if (b == std::string::npos) break;
    if (c >= a && c <= b + 2) {
      target = line.substr(a + 2, b - a - 2);
      target = target.substr(0, target.find('|'));
      target = target.substr(0, target.find('#'));
      return true;
    }
  }
  for (size_t a = line.find('['); a != std::string::npos; a = line.find('[', a + 1)) {
    size_t m = line.find("](", a), e = m == std::string::npos ? m : line.find(')', m);
    if (e == std::string::npos) break;
    if (c >= a && c <= e + 1) {
      target = line.substr(m + 2, e - m - 2);
      external = target.find("://") != std::string::npos;
      if (!external) target = target.substr(0, target.find('#'));
      return true;
    }
  }
  return false;
}

void EditorScreen::followLink(const std::string& target, bool external) {
  if (external) return app::toast("External link: " + target);
  std::string path = storage::resolveLink(target, path_);
  if (path == path_) return;  // link to a heading in this note
  save();
  if (!path.empty()) return reading_ ? app::openNote(path) : app::editNote(path);
  if (!reading_) {
    // Like Obsidian: following a link to a missing note creates it
    path = storage::createNote("/", target);
    if (path.empty()) return app::toast("Couldn't create note");
    return app::editNote(path);
  }
  app::confirm("Create note?", "\"" + target + "\" doesn't exist yet.", "Create", theme::ACCENT_BG,
               [target] {
                 std::string p = storage::createNote("/", target);
                 if (p.empty()) return app::toast("Couldn't create note");
                 app::editNote(p);
               });
}

void EditorScreen::followLinkAtCursor() {
  std::string target;
  bool external;
  if (!linkTargetAt(cursor_, target, external)) return app::toast("No link at cursor");
  followLink(target, external);
}

bool EditorScreen::linkAtTap(int line, int x, int ry, uint32_t* pos) {
  Layout L;
  layoutLine(line, false, L);
  int r = 0;
  while (r + 1 < (int)L.rows.size() && ry >= L.rows[r].top + L.rows[r].h) r++;
  for (auto& sg : L.segs) {
    if (!(sg.style & ST_LINK) || sg.kind != KD_TEXT) continue;
    if (sg.b <= L.rows[r].a || sg.a >= L.rows[r].b) continue;
    uint32_t a = std::max(sg.a, L.rows[r].a), b = std::min(sg.b, L.rows[r].b);
    int x1 = L.x[a], x2 = (b < L.rows[r].b) ? L.x[b] : L.rowEnd[r];
    if (x >= x1 - 6 && x <= x2 + 6) {
      *pos = lines_[line].start + a;
      return true;
    }
  }
  return false;
}

void EditorScreen::undo(bool redo) {
  auto& from = redo ? redo_ : undo_;
  auto& to = redo ? undo_ : redo_;
  if (from.empty()) return app::toast(redo ? "Nothing to redo" : "Nothing to undo", 1000);
  Op op = from.back();
  from.pop_back();
  if (redo) {
    replace(op.pos, op.pos + op.removed.size(), op.inserted, false);
    setCursor(op.cursorAfter, false);
  } else {
    replace(op.pos, op.pos + op.inserted.size(), op.removed, false);
    setCursor(op.cursorBefore, false);
  }
  to.push_back(op);
}

void EditorScreen::updatePopup() {
  bool was = popup_;
  std::string oldFirst = popupItems_.empty() ? "" : popupItems_[0];
  popup_ = false;
  if (hasSelection() || readOnly_) return;
  const uint32_t ls = lines_[cursorLine_].start;
  for (uint32_t k = cursor_; k >= ls + 2; k--) {
    if (text_[k - 1] == ']' && text_[k - 2] == ']') return;
    if (text_[k - 1] == '[' && text_[k - 2] == '[') {
      std::string q = text_.substr(k, cursor_ - k);
      if (q.size() > 60 || q.find_first_of("|#]") != std::string::npos) return;
      popupItems_ = storage::search(q, 6);
      popup_ = !popupItems_.empty();
      if (!was || popupItems_.empty() || popupStart_ != k || popupItems_[0] != oldFirst) {
        popupSel_ = 0;
        popupFirst_ = 0;
      }
      popupSel_ = std::min<int>(popupSel_, (int)popupItems_.size() - 1);
      popupStart_ = k;
      return;
    }
  }
}

void EditorScreen::acceptPopup() {
  if (!popup_ || popupItems_.empty()) return;
  std::string name = storage::linkText(popupItems_[popupSel_]);
  replace(popupStart_, cursor_, name);
  setCursor(popupStart_ + name.size(), false);
  if (text_.compare(cursor_, 2, "]]") == 0) {
    setCursor(cursor_ + 2, false);
  } else {
    insertText("]]");
  }
  popup_ = false;
}

void EditorScreen::afterChange(int prevLine, int editedLine, bool structural) {
  uint32_t t0 = micros();
  cursorLine_ = lineOf(cursor_);
  if (cursorLine_ != prevLine) {
    if (prevLine >= 0 && prevLine < (int)lines_.size()) lines_[prevLine].dirty = true;
    lines_[cursorLine_].dirty = true;
  }
  bool hadPopup = popup_;
  bool heights = relayout();
  uint32_t t1 = micros();
  updatePopup();
  bool scrolled = ensureCursorVisible();
  uint32_t t2 = micros();
  if (structural || heights || scrolled || popup_ || hadPopup) {
    drawContent();
  } else {
    drawLineOnly(cursorLine_);
    if (prevLine != cursorLine_) drawLineOnly(prevLine);
    if (editedLine >= 0 && editedLine != cursorLine_ && editedLine != prevLine) drawLineOnly(editedLine);
  }
  if (titleDirty_ != dirty_) drawTitle();
  drawStatus();
  uint32_t t3 = micros();
  if (t3 - t0 > 60000)
    Serial.printf("[editor] relayout %u us, popup/scroll %u us, draw %u us (full=%d)\n", (unsigned)(t1 - t0),
                  (unsigned)(t2 - t1), (unsigned)(t3 - t2), structural || heights || scrolled || popup_ || hadPopup);
}

// ===========================================================================
// Input

void EditorScreen::onKey(const Event& e) {
  using namespace input;
  const uint32_t t0 = millis();
  struct Timer {
    uint32_t t0;
    ~Timer() {
      uint32_t ms = millis() - t0;
      if (ms > 80) Serial.printf("[editor] slow key: %u ms\n", (unsigned)ms);
    }
  } timer{t0};
  if (reading_) return readingKey(e);
  const int prevLine = cursorLine_;
  const bool hadSel = hasSelection();
  const size_t linesBefore = lines_.size();
  const bool shift = e.shift(), ctrl = e.ctrl();
  bool keepGoal = false;

  if (popup_) {
    if (e.key == K_UP || e.key == K_DOWN) {
      int n = popupItems_.size();
      popupSel_ = (popupSel_ + (e.key == K_UP ? n - 1 : 1)) % n;
      drawContent();
      return;
    }
    if ((e.key == K_ENTER && !ctrl) || e.key == K_TAB) {
      acceptPopup();
      afterChange(prevLine, -1, true);
      return;
    }
    if (e.key == K_ESC) {
      popup_ = false;
      drawContent();
      return;
    }
  }

  switch (e.key) {
    case K_CHAR:
      if (ctrl) {
        switch (e.ch) {
          case 's':
            if (save()) app::toast("Saved", 800);
            drawStatus();
            return;
          case 'z': undo(shift); break;
          case 'y': undo(true); break;
          case 'a':
            anchor_ = 0;
            cursor_ = text_.size();
            break;
          case 'c':
          case 'x':
            if (!hasSelection()) return;
            clipboard_ = text_.substr(std::min(anchor_, cursor_), std::max(anchor_, cursor_) - std::min(anchor_, cursor_));
            if (e.ch == 'x') deleteSelection();
            else return app::toast("Copied", 600);
            break;
          case 'v':
            if (!clipboard_.empty()) insertText(clipboard_);
            break;
          case 'b': wrapSelection("**"); break;
          case 'i': wrapSelection("*"); break;
          case 'k':
            if (hasSelection()) {
              uint32_t a = std::min(anchor_, cursor_), b = std::max(anchor_, cursor_);
              replace(b, b, "]]");
              replace(a, a, "[[");
              setCursor(b + 4, false);
            } else {
              insertText("[[]]");
              setCursor(cursor_ - 2, false);
            }
            break;
          case 'l': toggleCheckbox(cursorLine_); break;
          case 'e': return app::viewNote(path_);
          default: return;
        }
      } else if (e.alt()) {
        return;
      } else if (e.ch == '[' && !hasSelection() && cursor_ > 0 && text_[cursor_ - 1] == '[' &&
                 (cursor_ >= text_.size() || text_[cursor_] != ']')) {
        insertText("[]]");
        setCursor(cursor_ - 2, false);
      } else if (e.ch == ']' && !hasSelection() && cursor_ < text_.size() && text_[cursor_] == ']') {
        setCursor(cursor_ + 1, false);
      } else {
        insertText(std::string(1, e.ch));
      }
      break;
    case K_ENTER:
      if (ctrl || e.alt()) return followLinkAtCursor();
      newline(shift);
      break;
    case K_BACKSPACE:
      if (hasSelection()) deleteSelection();
      else if (ctrl || e.alt()) replace(wordLeft(cursor_), cursor_, "");
      else if (cursor_ >= 2 && text_.compare(cursor_ - 2, 4, "[[]]") == 0)
        replace(cursor_ - 2, cursor_ + 2, "");
      else if (cursor_ > 0) replace(prevChar(cursor_), cursor_, "");
      setCursor(cursor_, false);
      break;
    case K_DELETE:
      if (hasSelection()) deleteSelection();
      else if (ctrl) replace(cursor_, wordRight(cursor_), "");
      else replace(cursor_, nextChar(cursor_), "");
      setCursor(cursor_, false);
      break;
    case K_TAB: {
      const char* p = text_.data() + lines_[cursorLine_].start;
      uint32_t n = lineLen(cursorLine_), k = 0;
      while (k < n && (p[k] == ' ' || p[k] == '\t')) k++;
      bool listLine = (k + 1 < n && strchr("-*+", p[k]) && p[k + 1] == ' ') ||
                      (k < n && isdigit((unsigned char)p[k]));
      bool multi = hasSelection() && lineOf(anchor_) != lineOf(cursor_);
      if (shift || listLine || multi) indentLines(shift);
      else insertText("\t");
      break;
    }
    case K_LEFT:
      if (hasSelection() && !shift) setCursor(std::min(anchor_, cursor_), false);
      else setCursor(ctrl ? wordLeft(cursor_) : prevChar(cursor_), shift);
      break;
    case K_RIGHT:
      if (hasSelection() && !shift) setCursor(std::max(anchor_, cursor_), false);
      else setCursor(ctrl ? wordRight(cursor_) : nextChar(cursor_), shift);
      break;
    case K_UP:
    case K_DOWN:
      moveVertical(e.key == K_UP ? -1 : 1, shift);
      keepGoal = true;
      break;
    case K_PGUP:
    case K_PGDN:
      for (int i = 0; i < viewH() / 24; i++) moveVertical(e.key == K_PGUP ? -1 : 1, shift);
      keepGoal = true;
      break;
    case K_HOME:
      setCursor(ctrl ? 0 : lines_[cursorLine_].start, shift);
      break;
    case K_END:
      setCursor(ctrl ? text_.size() : lines_[cursorLine_].start + lineLen(cursorLine_), shift);
      break;
    case K_ESC:
      if (!hasSelection()) return;
      setCursor(cursor_, false);
      break;
    case K_F2:
      return app::openSwitcher(app::SwitcherMode::Rename, storage::parentDir(path_), path_);
    default:
      return;
  }
  if (!keepGoal) goalX_ = -1;
  bool structural = lines_.size() != linesBefore || hadSel || hasSelection();
  afterChange(prevLine, -1, structural);
}

void EditorScreen::onTap(int x, int y) {
  if (ui::hitLeft(x, y)) return app::back();
  int slot = ui::hitRightSlot(x, y);
  if (slot == 1) return reading_ ? app::editNote(path_) : app::viewNote(path_);
  if (slot == 0) return app::noteMenu(path_);
  if (slot == 2 && !reading_) return app::toggleKeyboard();
  if (y < theme::BAR_H || y >= theme::BAR_H + viewH()) return;

  const int cy = y - theme::BAR_H;
  const int prevLine = cursorLine_;
  if (popup_ && x >= popupX_ && x < popupX_ + popupW_ && cy + scroll_ >= popupY_ &&
      cy + scroll_ < popupY_ + popupH_) {
    popupSel_ = std::max(0, std::min<int>(popupFirst_ + (cy + scroll_ - popupY_ - 4) / 28,
                                          popupItems_.size() - 1));
    acceptPopup();
    afterChange(prevLine, -1, true);
    return;
  }

  const int32_t docY = cy + scroll_;
  const bool pastEnd = docY >= tops_.back() + lines_.back().height;
  const int line = lineAtY(docY);
  if (!pastEnd && !revealedLine(line)) {
    const int ry = docY - tops_[line];
    // Rendered checkboxes toggle without moving the cursor
    Layout L;
    layoutLine(line, false, L);
    for (auto& sg : L.segs) {
      if (sg.kind != KD_CHECK && sg.kind != KD_CHECK_DONE) continue;
      const Row& row = L.rows[L.row[sg.a]];
      if (x >= L.x[sg.a] - 4 && x <= L.x[sg.a] + 24 && ry >= row.top && ry < row.top + row.h) {
        bool wasReadOnly = readOnly_;
        toggleCheckbox(line);
        if (wasReadOnly) return;
        if (reading_) {
          relayout();
          drawContent();
          drawTitle();
        } else {
          afterChange(prevLine, line, false);
        }
        return;
      }
    }
    // Tapping an embedded note's card opens that note
    if (L.block == BL_EMBED && !L.media.empty()) return reading_ ? app::openNote(L.media) : app::editNote(L.media);
    // Rendered links are followed, as in Obsidian's live preview
    uint32_t pos;
    if (linkAtTap(line, x, ry, &pos)) {
      std::string target;
      bool external;
      if (linkTargetAt(pos, target, external)) return followLink(target, external);
    }
  }
  if (reading_) return;
  setCursor(pastEnd ? text_.size() : hitTest(line, x, docY - tops_[line]), false);
  goalX_ = -1;
  afterChange(prevLine, -1, false);
}

void EditorScreen::scrollTo(int s) {
  s = std::max(0, std::min(s, std::max(0, totalH_ - viewH())));
  if (s != scroll_) {
    scroll_ = s;
    if (popup_) placePopup();
    drawContent();
  }
}

void EditorScreen::readingKey(const Event& e) {
  using namespace input;
  const int page = viewH() - 40;
  switch (e.key) {
    case K_UP: return scrollTo(scroll_ - 44);
    case K_DOWN: return scrollTo(scroll_ + 44);
    case K_PGUP: return scrollTo(scroll_ - page);
    case K_PGDN: return scrollTo(scroll_ + page);
    case K_HOME: return scrollTo(0);
    case K_END: return scrollTo(totalH_);
    case K_ENTER: return app::editNote(path_);
    case K_ESC:
    case K_BACKSPACE: return app::back();
    case K_F2: return app::openSwitcher(app::SwitcherMode::Rename, storage::parentDir(path_), path_);
    case K_CHAR:
      if (e.isChar('e') || e.isChar('e', M_CTRL)) return app::editNote(path_);
      if (e.isChar(' ')) return scrollTo(scroll_ + page);
      return;
    default:
      return;
  }
}

void EditorScreen::onDrag(int dy) {
  fastScroll_ = true;  // skip image decoding while the finger moves
  scrollTo(scroll_ - dy);
}

void EditorScreen::onDragEnd() {
  if (!fastScroll_) return;
  fastScroll_ = false;
  drawContent();
}
