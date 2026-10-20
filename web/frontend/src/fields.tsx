import { useEffect, useState } from "react";
import { hexToVec, vecToHex } from "./scene";
import type { Vec3 } from "./types";

// Number input that lets the user type freely ("-", "0.", "") and only
// commits finite numbers, clamped to [min, max].
export function NumberField(props: {
  label: string;
  value: number;
  onChange: (v: number) => void;
  step?: number;
  min?: number;
  max?: number;
  integer?: boolean;
  hint?: string;
}) {
  const { label, value, onChange, step = 0.1, min, max, integer, hint } = props;
  const [text, setText] = useState(String(value));
  useEffect(() => setText(String(value)), [value]);
  const commit = (t: string) => {
    let v = Number(t);
    if (t.trim() === "" || !Number.isFinite(v)) return;
    if (integer) v = Math.round(v);
    if (min !== undefined) v = Math.max(min, v);
    if (max !== undefined) v = Math.min(max, v);
    if (v !== value) onChange(v);
  };
  return (
    <label className="field" title={hint}>
      <span>{label}</span>
      <input
        type="number"
        inputMode="decimal"
        step={step}
        min={min}
        max={max}
        value={text}
        onChange={(e) => {
          setText(e.target.value);
          commit(e.target.value);
        }}
        onBlur={() => setText(String(value))}
      />
    </label>
  );
}

export function Vec3Field(props: { label: string; value: Vec3; onChange: (v: Vec3) => void; step?: number }) {
  const { label, value, onChange, step = 0.1 } = props;
  const axes = ["x", "y", "z"];
  return (
    <div className="field vec3">
      <span>{label}</span>
      <div className="triple">
        {axes.map((ax, i) => (
          <MiniNumber
            key={ax}
            axis={ax}
            value={value[i]}
            step={step}
            onChange={(v) => {
              const next = [...value] as Vec3;
              next[i] = v;
              onChange(next);
            }}
          />
        ))}
      </div>
    </div>
  );
}

function MiniNumber(props: { axis: string; value: number; step: number; onChange: (v: number) => void }) {
  const [text, setText] = useState(String(props.value));
  useEffect(() => setText(String(props.value)), [props.value]);
  return (
    <input
      aria-label={props.axis}
      type="number"
      step={props.step}
      value={text}
      onChange={(e) => {
        setText(e.target.value);
        const v = Number(e.target.value);
        if (e.target.value.trim() !== "" && Number.isFinite(v)) props.onChange(v);
      }}
      onBlur={() => setText(String(props.value))}
    />
  );
}

// Colour picker for linear RGB values in [0,1] (converted through sRGB).
export function ColorField(props: { label: string; value: Vec3; onChange: (v: Vec3) => void }) {
  return (
    <label className="field">
      <span>{props.label}</span>
      <span className="color">
        <input type="color" value={vecToHex(props.value)} onChange={(e) => props.onChange(hexToVec(e.target.value))} />
        <code>{props.value.map((c) => c.toFixed(2)).join("  ")}</code>
      </span>
    </label>
  );
}

export function SelectField<T extends string>(props: {
  label: string;
  value: T;
  options: readonly T[] | { value: T; label: string }[];
  onChange: (v: T) => void;
}) {
  const opts = (props.options as readonly (T | { value: T; label: string })[]).map((o) =>
    typeof o === "string" ? { value: o, label: o } : o,
  );
  return (
    <label className="field">
      <span>{props.label}</span>
      <select value={props.value} onChange={(e) => props.onChange(e.target.value as T)}>
        {opts.map((o) => (
          <option key={o.value} value={o.value}>
            {o.label}
          </option>
        ))}
      </select>
    </label>
  );
}

export function CheckField(props: { label: string; value: boolean; onChange: (v: boolean) => void }) {
  return (
    <label className="field check">
      <span>{props.label}</span>
      <input type="checkbox" checked={props.value} onChange={(e) => props.onChange(e.target.checked)} />
    </label>
  );
}

export function TextField(props: { label: string; value: string; onCommit: (v: string) => void }) {
  const [text, setText] = useState(props.value);
  useEffect(() => setText(props.value), [props.value]);
  return (
    <label className="field">
      <span>{props.label}</span>
      <input
        type="text"
        value={text}
        onChange={(e) => setText(e.target.value)}
        onBlur={() => text.trim() && text !== props.value ? props.onCommit(text.trim()) : setText(props.value)}
        onKeyDown={(e) => e.key === "Enter" && (e.target as HTMLInputElement).blur()}
      />
    </label>
  );
}
