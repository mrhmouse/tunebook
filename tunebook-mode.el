;;; tunebook-mode.el -*- lexical-binding:t -*-

;; MIT

;; Maintainer: jordan@yonder.computer
;; Keywords: tunebook, languages

;;; Code:

(defcustom tunebook-mode-hook nil
  "Normal hook ran when entering tunebook mode"
  :type 'hook
  :group 'tunebook)

(defvar tunebook-mode-syntax-table
  (let ((table (make-syntax-table)))
    (modify-syntax-entry (cons ?a ?z) "w" table)
    (modify-syntax-entry ?\( "(" table)
    (modify-syntax-entry ?\) ")" table)
    (modify-syntax-entry ?# "<" table)
    (modify-syntax-entry ?\n ">" table)
    (modify-syntax-entry ?\" "\"" table)
    table)
  "Syntax table used in `tunebook-mode'")

(defvar tunebook-font-lock-keywords
  (let ((keywords
         (cons
          (string-join (list "\\("
                             (string-join
                              '("add" "am" "attack" "base" "clip" "decay" "detune"
                                "fm" "groove" "hz" "instrument" "modulate" "noise"
                                "pm" "release" "repeat" "rest" "root" "r" "saw"
                                "section"  "sine" "sin" "song" "sqr" "square" "sub"
                                "sustain" "tempo" "triangle" "tri" "voice" "volume")
                              "\\|")
                             "\\)"))
          'font-lock-keyword-face)))
    (cons keywords
          '(("\\d+" . font-lock-constant-face)
            ("\\d+/\\d+" . font-lock-constant-face)
            ("\\d+\\\\\\d+" . font-lock-constant-face)
            ("#.*" . font-lock-comment-face)
            ("\"[^\"]*\"" . font-lock-string-face))))
  "Keywords to highlight in tunebook mode")

(defvar tunebook-mode-map
  (let ((map (make-sparse-keymap)))
    map)
  "Keymap for tunebook mode")

(define-derived-mode tunebook-mode fundamental-mode "tunebook"
  "Major mode for editing tunebooks"
  (setq-local indent-tabs-mode nil)
  (setq-local comment-start "# ")
  (setq-local words-include-escapes nil)
  (setq-local font-lock-defaults '(tunebook-font-lock-keywords)))
