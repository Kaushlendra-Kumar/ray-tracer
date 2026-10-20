package main

// Parser for the rt CLI's --stream protocol (see apps/rt_cli.cpp):
//
//	FRAME <json header>\n<png bytes, length = header.bytes>
//	DONE <json stats>\n
//	ERROR <json {"message": ...}>\n

import (
	"bufio"
	"encoding/json"
	"fmt"
	"io"
	"strings"
)

// FrameHeader mirrors the JSON header written before each PNG frame.
type FrameHeader struct {
	Pass      int     `json:"pass"`
	SppDone   int     `json:"spp_done"`
	SppTarget int     `json:"spp_target"`
	ElapsedMs float64 `json:"elapsed_ms"`
	Rays      uint64  `json:"rays"`
	Width     int     `json:"width"`
	Height    int     `json:"height"`
	Finished  bool    `json:"finished"`
	Cancelled bool    `json:"cancelled"`
	Bytes     int     `json:"bytes"`
}

// Message is one decoded protocol message.
type Message struct {
	Kind   string // "frame", "done" or "error"
	Frame  FrameHeader
	PNG    []byte
	Raw    json.RawMessage // DONE stats or ERROR body
	ErrMsg string
}

// maxFrameBytes bounds memory use if the renderer ever misbehaves.
const maxFrameBytes = 64 << 20

// ReadMessages decodes messages from r until EOF, calling fn for each.
func ReadMessages(r io.Reader, fn func(Message)) error {
	br := bufio.NewReaderSize(r, 1<<16)
	for {
		line, err := br.ReadString('\n')
		if err == io.EOF && line == "" {
			return nil
		}
		if err != nil && err != io.EOF {
			return err
		}
		line = strings.TrimRight(line, "\r\n")
		kind, body, _ := strings.Cut(line, " ")
		switch kind {
		case "FRAME":
			var h FrameHeader
			if err := json.Unmarshal([]byte(body), &h); err != nil {
				return fmt.Errorf("bad FRAME header: %w", err)
			}
			if h.Bytes <= 0 || h.Bytes > maxFrameBytes {
				return fmt.Errorf("bad FRAME size %d", h.Bytes)
			}
			png := make([]byte, h.Bytes)
			if _, err := io.ReadFull(br, png); err != nil {
				return fmt.Errorf("truncated frame: %w", err)
			}
			fn(Message{Kind: "frame", Frame: h, PNG: png})
		case "DONE":
			fn(Message{Kind: "done", Raw: json.RawMessage(body)})
		case "ERROR":
			var e struct {
				Message string `json:"message"`
			}
			_ = json.Unmarshal([]byte(body), &e)
			fn(Message{Kind: "error", Raw: json.RawMessage(body), ErrMsg: e.Message})
		case "":
			// tolerate blank lines
		default:
			return fmt.Errorf("unexpected line from renderer: %.80q", line)
		}
		if err == io.EOF {
			return nil
		}
	}
}
