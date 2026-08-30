import { createRequire } from "node:module";
import schema from "./profile.schema.json" with { type: "json" };
import exampleProfiles from "./profile.examples.json" with { type: "json" };

type AjvLike = {
  compile: (schema: unknown) => AjvValidationFunction;
};

type AjvValidationFunction = ((data: unknown) => boolean) & { errors?: unknown[] | null };

type AjvModuleConstructor = new (options?: Record<string, unknown>) => AjvLike;

const require = createRequire(import.meta.url);
const ajvModule = require("ajv/dist/2020.js") as AjvLike & { default?: AjvModuleConstructor };
const AjvCtor = (ajvModule.default as AjvModuleConstructor) ?? (ajvModule as unknown as AjvModuleConstructor);
const ajv = new AjvCtor({ strict: true });
const validateSchema = ajv.compile(schema);

export type ProfileValidationIssue = {
  path: string;
  message: string;
};

export type ProfileValidationResult = {
  valid: boolean;
  errors: ProfileValidationIssue[];
};

export interface ParsedProfile {
  readonly value: unknown;
}

function isFiniteNum(v: unknown): v is number {
  return typeof v === "number" && Number.isFinite(v);
}

function collectFieldError(path: string, message: string, issues: ProfileValidationIssue[]) {
  issues.push({ path, message });
}

function schemaValidationIssues(validator: AjvValidationFunction): ProfileValidationIssue[] {
  const raw = validator.errors ?? [];
  return (raw as Array<{ instancePath?: string; keyword?: string; message?: string | null }>).map((err) => ({
    path: err.instancePath || "/",
    message: `${err.keyword}: ${err.message ?? "invalid"}`
  }));
}

export function parseProfile(profile: unknown): ProfileValidationResult {
  const issues: ProfileValidationIssue[] = [];
  if (!validateSchema(profile)) {
    issues.push(...schemaValidationIssues(validateSchema));
    return { valid: false, errors: issues };
  }

  const p = profile as Record<string, unknown>;

  const haptic = p["haptic"] as Record<string, unknown>;
  const endstops = haptic["endstops"] as Record<string, unknown>;
  const safety = p["safetyUserCaps"] as Record<string, unknown>;
  const actions = (p["actions"] as Array<Record<string, unknown>> | undefined) ?? [];

  if (haptic["mode"] === "free") {
    // free 模式不限制 detent 形状，只要求数值字段在 schema 内部合法。
  }
  if (haptic["mode"] === "texture") {
    if (!haptic["texture"]) {
      collectFieldError("/haptic/texture", "mode=texture 时必须提供 texture 段", issues);
    }
  }
  if (endstops["enabled"] === true) {
    const min = endstops["minPosition"] as number;
    const max = endstops["maxPosition"] as number;
    if (!isFiniteNum(min) || !isFiniteNum(max) || min >= max) {
      collectFieldError("/haptic/endstops", "endstops.enabled 为 true 时 minPosition 必须小于 maxPosition", issues);
    }
  }
  const continuous = safety["continuousTorquemNm"] as number;
  const transient = safety["transientTorquemNm"] as number;
  if (isFiniteNum(continuous) && isFiniteNum(transient) && continuous > transient) {
    collectFieldError("/safetyUserCaps", "连续力矩上限不能超过瞬态上限", issues);
  }
  if (
    !Array.isArray(actions) ||
    actions.some((a) => typeof a["deadband"] === "number" && a["deadband"] < 0)
  ) {
    collectFieldError("/actions", "action deadband 不允许小于0", issues);
  }

  return { valid: issues.length === 0, errors: issues };
}

export function loadExampleProfiles(): unknown[] {
  return exampleProfiles as unknown[];
}

export function validateProfile(profile: unknown): boolean {
  return parseProfile(profile).valid;
}
