#pragma once
#include <Arduino.h>

#include "epub/ByteFeed.h"
#include "epub/XmlPull.h"
#include "text/Content.h"

// Turns a chapter's XHTML into the fragment stream the layout engine consumes.
//
// CSS is deliberately ignored: tags map to a fixed set of block styles. On a
// 2.19" column the difference is barely visible, and a CSS cascade would cost
// far more RAM than this device has.
class XhtmlSource : public ContentSource {
 public:
  bool begin(ByteFeed* feed);

  SourceState state() const override;
  bool next(Frag& out) override;
  bool restore(const SourceState& s) override;
  uint32_t sizeBytes() const override { return feed_ ? feed_->size() : 0; }

 private:
  bool wordFromRun(Frag& out);
  void loadRun();
  void resetState();

  XmlPull xml_;
  ByteFeed* feed_ = nullptr;

  // The text run currently being split into words.
  char run_[XmlPull::TEXT_MAX];
  uint32_t runOffset_ = 0;  // feed offset where the run's token begins
  uint16_t runPos_ = 0;
  uint16_t runLen_ = 0;
  uint16_t runFrags_ = 0;  // words already emitted from this run

  BlockStyle block_ = BlockStyle::Paragraph;
  uint8_t emphasis_ = 0;
  uint8_t listDepth_ = 0;

  // Whitespace bookkeeping for Frag::joinPrev. Inline tags carry no
  // whitespace of their own, so "a <em>b</em>" and "a<em>b</em>" differ only
  // by what was consumed between the two text runs.
  bool sawSpace_ = false;
  bool lastWasWord_ = false;

  // Name of the element whose content is being discarded (head/script/style).
  char skipUntil_[XmlPull::TAG_NAME_MAX] = {0};
};
