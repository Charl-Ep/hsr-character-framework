/** JSON script interfaces. Field reference: script-api.md. */
/** Nonempty string without NUL. */
export type Name = string;
/** Integral number. */
export type Integer = number;
export type Vec3 = [number, number, number];
/** Quaternion [x, y, z, w], nonzero. Default identity is [0, 0, 0, 1]. */
export type Quaternion = [number, number, number, number];
export type NonEmpty<T> = [T, ...T[]];
export type Table<T> = Record<string, T>;
export type Value = boolean | number | string;
export type Values = Table<Value>;
export type EffectKey = Name;
export type FunctionKey = Name;
export type ActionKey = Name;
export type ContextKey = Name;
/** "body" or "partner:KEY". KEY is a declared partner key. */
export type Participant = "body" | `partner:${string}`;
/** "Caster", "partner:KEY", or a named target supplied by bindings/defaults. */
export type Target = Name;
/** Files with suffix .script.json are discovered under the Scripts directory. */
export interface ScriptFile {
    format: "hksr.script";
    id: Name;
    /** Official character configuration identity. */
    applies_to: Name;
    /** One script for a source is selected automatically unless false.
     * Multiple candidates require one explicitly true default, or all false.
     */
    default?: boolean;
    /** Native event binding scope; defaults to id. */
    scope?: Name;
    /** Definition modules, relative to the Scripts directory. */
    imports?: Name[];
    templates?: Table<ActionDefinition>;
    defaults?: {
        targets?: Table<{ alias: Target } | { local_position: Vec3 }>;
        choices?: Table<string>;
        anchors?: Table<SpatialAnchor>;
    };
    actions: Table<ActionDefinition>;
    functions?: Table<Step[]>;
    states?: Table<StateDefinition>;
    initial_states?: ContextKey[];
    resources?: Resources;
    restore?: VisualInstruction[];
    signals?: Table<VisualInstruction[] | { context: ContextKey; run: VisualInstruction[] }>;
}
export interface SpatialAnchor {
    position: Vec3;
    rotation?: Quaternion;
}

// Named declarations are reused as metadata; engine resources load on use.
export interface Resources {
    effects?: Table<EffectDefinition>;
    partners?: Table<PartnerDefinition>;
    phases?: Table<DefinitionSource>;
    projectiles?: Table<DefinitionSource>;
}
/** Definition module loaded by a script's imports. */
export interface ScriptModule {
    imports?: Name[];
    templates?: Table<ActionDefinition>;
    functions?: Table<Step[]>;
    states?: Table<StateDefinition>;
    resources?: Resources;
    defaults?: ScriptFile["defaults"];
}
export interface EffectCommon {
    path: Name;
    /** Explicit scale override. */
    scale?: Vec3;
    unique_name?: Name;
    /** @default true */
    fade_in?: boolean;
    /** @default false */
    simulate_immediately?: boolean;
    /** @default false */
    resident?: boolean;
    mutex?: { interval: number; maximum: Integer; scope: "global" | "entity" };
    /** @default false */
    enable_replay?: boolean;
}
export type EffectDefinition = EffectCommon & (
    | {
        target: Target;
        anchor?: never;
        /** @default [0,0,0] */
        position_offset?: Vec3;
        /** Euler degrees. @default [0,0,0] */
        rotation_offset?: Vec3;
        /** @default false */
        attach_to_target?: boolean;
        attach_point?: Name;
        /** True requires attach_to_target=false. @default false */
        allow_position?: boolean;
    }
    | {
        anchor: Name;
        target?: never;
        position_offset?: never;
        rotation_offset?: never;
        attach_to_target?: never;
        attach_point?: never;
        allow_position?: false;
    }
);
export interface PartnerDefinition {
    model: Name;
    config: Name;
    /** Unique member name within this program. */
    member: Name;
}
export interface DefinitionSource {
    root: { kind: "ability" | "global_modifier" | "global_template"; name: Name };
    path: NonEmpty<DefinitionField>;
}
export type DefinitionField = { owner: Name; field: Name } & (
    | { index: Integer; key?: never }
    | { key: Name; index?: never }
    | { index?: never; key?: never }
);
export interface VisualState {
    enter?: VisualInstruction[];
    exit?: VisualInstruction[];
    /** Unique event scope within this program. */
    event_scope?: Name;
    /** Key in resources.phases. */
    configuration?: Name;
    restore?: VisualInstruction[];
    /** Requires restore. Keys of live effects retained across reconstruction. */
    restore_effects?: EffectKey[];
}
/** A composite animation unit; it must be the only item in an action's run. */
export interface CuedAnimation {
    op: "animation.cued";
    state: Name;
    /** @default false */
    restart_scope?: boolean;
    prepare?: VisualInstruction[];
    /** Each lane is ordered by strictly increasing normalized body time. */
    tracks?: VisualCue[][];
    /** @default 1 */
    complete_at?: number;
    then?: { state: Name; at?: number; run?: VisualInstruction[] };
}
export interface VisualCue {
    at: number;
    run: VisualInstruction[];
}

// Tasks, explicit inputs and conditions
export type InputDefinition = InputMetadata & (
    | { type: "bool"; default?: boolean; choices?: NonEmpty<InputChoice<boolean>> }
    | { type: "integer"; default?: Integer; choices?: NonEmpty<InputChoice<Integer>> }
    | { type: "number"; default?: number; choices?: NonEmpty<InputChoice<number>> }
    | { type: "text"; default?: string; choices?: NonEmpty<InputChoice<string>> }
);
export interface InputMetadata {
    label?: Name;
    description?: Name;
    /** @default false */
    diagnostic?: boolean;
}
export interface InputChoice<T extends Value> {
    value: T;
    label: Name;
    /** @default false */
    diagnostic?: boolean;
}
export type Condition =
    | { op: "value"; name: Name; is: Value; context?: ContextKey }
    | { op: "one_of"; name: Name; values: Value[]; context?: ContextKey }
    | { op: "number"; name: Name; compare: "eq" | "ne" | "lt" | "le" | "gt" | "ge";
        value: number; context?: ContextKey }
    | { op: "phase"; is: Name }
    | { op: "participant"; name: Participant }
    | { op: "target"; name: Target; allow_position?: boolean }
    | { op: "anchor"; name: Name }
    | { op: "choice"; name: Name; is: Name }
    | { op: "state"; name: ContextKey; present?: boolean } // default true
    | { op: "playback"; name: "selected" | "performance"; is: Value; values?: never }
    | { op: "playback"; name: "selected" | "performance"; values: Value[]; is?: never }
    | { op: "unresolved"; source: Name; detail: Name };
export type ActionDefinition = OrdinaryAction | { run: [CuedAnimation] } | ActionReference;
export type ActionReference = { use: Name; inputs?: Table<Partial<InputDefinition>> }
    & Partial<Omit<OrdinaryAction, "inputs">>;
export interface OrdinaryAction {
    inputs?: Table<InputDefinition>;
    requires?: Condition[];
    tags?: Name[];
    /** Omit for an explicitly unimplemented/query-only entry. */
    run?: Step[];
    on_interrupt?: Step[];
    next?: ActionKey | { action: ActionKey; inputs?: Values };
    runtime?: { policy?: "ready" | "performance" | "continuous" | "setting"; selected?: string };
    bindings?: { targets?: Target[]; anchors?: Name[]; choices?: Table<Name[]> };
    presentation?: {
        label?: Name;
        omissions?: Name[];
        /** @default true */
        listed?: boolean;
        /** setting/hit_response require mode=input and nonempty contexts. @default "action" */
        kind?: "action" | "setting" | "hit_response";
        contexts?: ContextKey[];
        ready?: { config: Name; skill: Integer; variant?: Integer }; // variant defaults to -1
    };
    /** @default "replace" */
    mode?: "replace" | "input";
    /** @default "user" */
    trigger?: "user" | "internal" | "event";
    on?: Partial<Record<"accepted" | "started" | "completed" | "interrupted" | "replaced" | "failed", StateWrite[]>>;
}
export interface StateWrite {
    context: ContextKey;
    values: Values;
}
export type VisualEvent =
    | "after_hit" | "after_hit_all" | "after_hit_miss" | "after_attacked_adv"
    | "after_attack" | "after_attacked" | "after_attack_end" | "after_attacked_end"
    | "after_damage_chunk" | "after_being_damage_chunk" | "being_limbo" | "before_dying";
export type DeathOccurrence = "default" | "die" | "die_hit" | "die_fly" | "die_limbo";
export interface StateDefinition extends VisualState {
    values?: Values;
    phase?: Name;
    /** Logical variables and numeric bounds with an empty resource recipe. @default false */
    state_only?: boolean;
    callbacks?: {
        enter?: FunctionKey;
        exit?: FunctionKey;
        reapply?: FunctionKey;
        restore?: FunctionKey;
        signals?: Table<FunctionKey>;
        events?: Partial<Record<VisualEvent, FunctionKey>>;
        /** @default "after_enter" */
        activation?: "after_enter" | "before_callbacks";
    };
    instances?: {
        name: Name;
        category?: Name;
        receiver?: Target;
        caster?: Target;
        ability?: Name;
        /** @default "receiver" */
        match?: "receiver" | "receiver_caster" | "receiver_caster_ability";
        /** @default "keep" */
        repeat?: "keep" | "replace_inputs" | "new";
    };
    numbers?: Table<{ initial: number; min?: number; max?: number }>;
    hit_effects?: HitEffectOverride[];
    /** Additive-hit weight category S through D (0 through 4). */
    resilience?: 0 | 1 | 2 | 3 | 4;
    visual_reaction?: { source: Name; flag: Integer; occurrence?: DeathOccurrence };
}
export interface HitEffectOverride {
    stacking: "highest" | "multiple";
    /** null leaves the path unset; an empty string is an explicit empty path. */
    path: string | null;
    /** @default "" */
    death_path?: string;
    priority: Integer;
    /** @default 1 */
    count?: Integer;
    /** @default 0 */
    death_count?: Integer;
    offset?: Vec3;
    keep_original?: boolean;
    replay_on_mutex?: boolean;
}

// Arrays submit steps sequentially. Use waits to sequence playback completion.
export interface NodeOrigin { source?: Name }
export interface Request { as?: Name; wait?: Exclude<WaitSignal, "progress"> }
export type Step = NodeOrigin & (
    | { op: "switch"; name: Name; cases: { is: Value; run: Step[]; label?: Name }[]; default?: Step[] }
    | { op: "call"; function: FunctionKey }
    | { op: "if"; condition: Condition; then: Step[];
        elif?: { condition: Condition; then: Step[]; source?: Name }[]; else?: Step[] }
    | { op: "parallel"; branches: Step[][] }
    | { op: "for_each"; bind: Target; targets: Target[]; collection?: Name; run: Step[]; empty?: Step[] }
    | { op: "start"; id: Name; run: Step[]; join?: boolean }
    | { op: "join" }
    | { op: "repeat"; run: Step[]; count: Integer; until_finish?: boolean }
    | { op: "repeat"; run: Step[]; count?: never; until_finish: true }
    | { op: "on_success"; for: Name; run: Step[] }
    | { op: "wait"; for: Name; until?: Exclude<WaitSignal, "progress">; progress?: never }
    | { op: "wait"; for: Name; until?: "progress"; progress: number }
    | { op: "wait"; seconds: number }
    | { op: "wait"; frame: true }
    | { op: "skip"; as: Name; reason: Name; unit?: Name; dependencies?: Name[] }
    | (Request & TaskCommand)
);
/** wait defaults to performance. These are separate lifecycle facts. */
export type WaitSignal = "submitted" | "started" | "animation_end" | "animation_finished"
    | "state_left" | "accepted" | "performance" | "drained" | "progress";
export type TaskCommand = Animation | Expression | Sound | ReadyPose | Reaction | Hit
    | AnimationParameter | Movement | Projectiles | ContextOperation | ContextValue | PlaybackValue
    | AttackBoundary | EmitVisualEvent | VisualInstruction;

export interface AnimationOptions {
    op: "animation";
    /** @default "body" */
    participant?: Participant;
    state: Name;
    /** @default 0 */
    layer?: Integer;
    /** Normalized blend duration. @default 0 */
    transition?: number;
    /** Normalized start offset. @default 0 */
    start?: number;
    /** @default true */
    restart?: boolean;
    /** Resolve the active reaction/default return pose. @default false */
    current_idle?: boolean;
}
/** end observes an animation boundary. Track defaults:
 * extra=0, release_on=[], force=false. previous_end is an explicit handoff point.
 */
export type Animation = AnimationOptions & (
    | { track?: never; end?: number }
    | { track: AnimationTrack; end: number }
);
export interface AnimationTrack {
    logic: Integer;
    extra?: Integer;
    release_on?: Integer[];
    force?: boolean;
    previous_end?: number;
}
export interface Expression {
    op: "expression";
    state: Name;
    /** @default 0.2 */
    transition?: number;
    current_idle?: boolean;
}
/** participant defaults to body. */
export interface Sound { op: "sound"; participant?: "body"; event: Name }
/** skill >= 0, variant defaults to -1. */
export interface ReadyPose { op: "ready_pose"; config: Name; skill: Integer; variant?: Integer }
/** recover defaults to false, occurrence to default. */
export interface Reaction { op: "reaction"; config: Name; flag: Integer; recover?: boolean; occurrence?: DeathOccurrence }
export interface Hit {
    op: "hit";
    target: Target;
    /** Empty string selects no source animation. */
    animation: string;
    /** Presence supplies angles even when both default to zero. */
    direction?: { horizontal?: number; vertical?: number };
    effect?: HitEffect;
    allow_position?: boolean;
    events?: [] | ["after_hit", "after_hit_all"] | ["after_hit_all"] | ["after_hit_miss"];
}
export interface HitEffect {
    path: string;
    /** @default -1 */
    height?: number;
    /** @default 0 */
    angle?: number;
    /** @default 0 */
    delay?: number;
    /** @default 0 */
    minimum_interval?: number;
    /** @default 0 */
    maximum_count?: Integer;
    /** @default false */
    offset_by_scale?: boolean;
}
export type ParameterValue =
    | { type: "float" | "int"; value: number }
    | { type: "bool"; value: boolean }
    | { type: "trigger" | "reset_trigger"; value?: true };
export interface AnimationParameter {
    op: "animation_parameter";
    participant: "body";
    parameter: { name: Name } & ParameterValue;
    /** @default false */
    propagate?: boolean;
}
export type ContextOperation =
    | { op: "state.enter"; name: ContextKey }
    | { op: "state.exit"; name: ContextKey; all?: boolean; only_caster?: boolean }
    | { op: "state.signal"; name: ContextKey; event: Name };
export type ContextValue = { op: "state.value"; name: ContextKey; variable: Name } & ValueChange;
export type PlaybackValue = { op: "playback.value"; variable: "selected" | "performance"; operation: "set" } & (
    | { value: string; value_input?: never }
    | { value_input: Name; value?: never }
);
export type ValueChange = (
    | { operation: "set"; value: Value; value_from?: never; value_input?: never }
    | { operation: "add" | "multiply"; value: number; value_from?: never; value_input?: never }
    | { operation: "set" | "add" | "multiply"; value_from: { context: ContextKey; name: Name };
        value?: never; value_input?: never }
    | { operation: "set"; value_input: Name; value?: never; value_from?: never }
);
export type AttackBoundary =
    | { op: "attack.record"; target: Target; allow_position?: boolean }
    | { op: "attack.finish" | "attack.complete" | "attack.chunk_begin" | "attack.chunk_end" };
export interface EmitVisualEvent {
    op: "event.emit";
    event: VisualEvent;
    recipient: Target;
    /** @default "Caster" */
    sender?: Target;
    allow_position?: boolean;
}
export type Movement = { op: "movement" } & (
    | { clock: "instant"; animation?: never; force_start?: false; segments: [InstantSegment] }
    | { clock?: "animation" | "speed"; animation: Name; force_start?: boolean; segments: NonEmpty<MovementSegment> }
);
// Animation/speed movement binds the request key supplied by movement.animation.
export interface SegmentOptions {
    offset?: Vec3;
    /** @default true */
    preserve_height?: boolean;
    /** Nonnegative; must be zero when sample is supplied. @default 0 */
    stop_distance?: number;
    sample?: MovementSample;
    /** Only with destination=target. @default false */
    target_geometry?: boolean;
}
export type Destination = { destination: "origin"; name?: never }
    | { destination: "anchor" | "target"; name: Name };
export type InstantSegment = Destination & SegmentOptions;
export type MovementSegment = InstantSegment & {
    /** Animation clock interval requires end > start. @default 0 */
    start?: number;
    /** @default 1 */
    end?: number;
    /** Positive for speed clock. @default 1 */
    speed?: number;
    /** @default -1 */
    exit_time?: number;
};
export interface MovementSample {
    percentage: number;
    distance: number;
    direction?: Vec3;
    /** Requires a target/anchor pose and zero direction. @default false */
    behind_target?: boolean;
    /** @default false */
    include_radii?: boolean;
}
export interface Projectiles {
    op: "projectiles";
    shots: ProjectileShot[];
}
export interface ProjectileShot {
    /** Scheduled group time; nonnegative. @default 0 */
    at?: number;
    /** Key in resources.projectiles. */
    parameters: Name;
    target: Target;
    values?: Table<number>;
    /** @default true */
    ordered?: boolean;
}

// Visual instructions. The resource key and context select Framework-owned receipts.
export interface InstructionOrigin {
    source?: Name;
    when?: { choice: Name; is: Name };
}
export type VisualInstruction = InstructionOrigin & (
    | ({ context?: ContextKey | null } & ScopedVisualOperation)
    | ({ context?: null } & (
        | { op: "variable.set"; variable: Name; value: number }
        | { op: "scope.enter" | "scope.exit"; name: ContextKey; fade?: boolean } // default true
    ))
);
/** Defaults by operation:
 * effect.remove/effect.remove_event: fade=true, immediate=false; unbind=false.
 * effect.progress: seconds=0. effect.trigger/effect.state: reset/restart=false, transition=0.
 * facing.target: ratio=1, angle_offset=0, duration=-1.
 * attachment.visibility: delay=0. animation.layer_weight: duration=0.
 * Timeline: type=0, wrap_mode=0, release_volume=false, shared_environment=false.
 * Supported Timeline arguments: type=0 (Common), release_volume=false.
 */
export type ScopedVisualOperation =
    | { op: "effect.create"; effects: NonEmpty<EffectKey> }
    | { op: "effect.remove"; effects: NonEmpty<EffectKey>; fade?: boolean; immediate?: boolean; optional?: NonEmpty<EffectKey> }
    | { op: "effect.remove_event"; path: Name; fade?: boolean; immediate?: boolean; unbind?: boolean }
    | { op: "effect.progress"; effect: EffectKey; value: number | { variable: Name }; seconds?: number }
    | { op: "effect.trigger"; effects: NonEmpty<EffectKey>; trigger: Name; reset?: boolean }
    | { op: "effect.follow"; effect: EffectKey; targets: Target[] }
    | { op: "effect.state"; effect: EffectKey; state: Name; transition?: number; restart?: boolean }
    | EffectParameter | EffectLayout | PartnerOperation | RenderingOperation | ModelOperation | BgmSync | ActorTimeScale
    | { op: "facing.target"; target: Target; ratio?: number; angle_offset?: number; duration?: number }
    | { op: "attachment.visibility"; points: NonEmpty<Name>; visible: boolean; delay?: Integer }
    | { op: "attachment.scale"; points: NonEmpty<Name>; scale: Vec3 }
    | { op: "animation.layer_weight"; layer: Name; weight: number; duration?: number }
    | ({ op: "animation.speed" } & ({ speed: number; reset?: never } | { reset: true; speed?: never }))
    | { op: "timeline.play"; key: Name; path: Name; type?: 0; wrap_mode?: 0 | 1 | 2;
        release_volume?: false; shared_environment?: boolean }
    | { op: "timeline.stop"; key: Name; type?: 0; wrap_mode?: 0 | 1 | 2;
        release_volume?: false; shared_environment?: boolean };
export type EffectParameter = {
    op: "effect.parameter";
    effect: EffectKey;
    name: Name;
    /** @default "first_active" */
    selection?: "first_active" | "first_matching_parameter";
} & (
    | { type: "float" | "int"; value: number }
    | { type: "bool"; value: boolean }
    | { type: "trigger"; value?: never }
);
/** show/transfer fade defaults to false; transfer requires a distinct target. */
export type EffectLayout = { op: "effect.layout"; effect: EffectKey } & (
    | { command: "hide"; fade?: never; target?: never; perform?: never }
    | { command: "show"; fade?: boolean; target?: never; perform?: never }
    | { command: "perform"; perform: 0 | 1 | 2 | 3; fade?: never; target?: never }
    | { command: "transfer"; target: EffectKey; fade?: boolean; perform?: never }
);
export type VisibilityFollow = "none" | "visible" | "invisible" | "follow"
    | "transmit_visible" | "transmit_invisible" | "transmit";
/** visibility defaults to none; attachment/mute flags to false.
 * partner.idle: normalized_start=0, normalized_transition=0, normalized_end=1.
 */
export type PartnerOperation = { partner: Name } & (
    | { op: "partner.create" | "partner.remove" }
    | { op: "partner.attach"; target: Target; self_point: Name; target_point: Name;
        disable_when_invisible?: boolean; use_target_scale?: boolean; visibility?: VisibilityFollow }
    | { op: "partner.animation_follow"; target: Target | null }
    | { op: "partner.visibility_follow"; target: Target | null; visibility?: VisibilityFollow }
    | { op: "partner.visibility"; key: Name; visible: boolean;
        mute_collider?: boolean; mute_triggers?: boolean; mute_audio?: boolean }
    | { op: "partner.idle"; state: Name; normalized_start?: number; normalized_transition?: number; normalized_end?: number }
);
/** render.outline requires reset=true or at least one added/removed name. */
export type RenderingOperation = { recipient?: Target } & (
    | { op: "render.mask"; mask: Integer; enabled: boolean; node?: Name | null }
    | { op: "render.outline"; reset?: boolean; add?: Name[] | null; remove?: Name[] | null }
    | { op: "render.visibility"; key: Name; visible: boolean }
);
/** active defaults to true; fade/finite_only to false; delay/fades to zero.
 * Active requests accept factor, delay, fade_in, fade_out and positive duration.
 * Inactive requests select a key or finite_only.
 */
export type ActorTimeScale = { op: "actor.time_scale"; recipient?: Target; fade?: boolean } & (
    | { active?: true; key: Name; finite_only?: false; factor: number;
        delay?: number; fade_in?: number; fade_out?: number; duration?: number }
    | { active: false; key: Name; finite_only?: false }
    | { active: false; finite_only: true; key?: never }
);
/** expression.enable: transit_out=0, transit_in=0.2. expression.material: duration=0.
 * model.character_scale: equal_ratio_by_min=false.
 */
export type ModelOperation =
    | { op: "model.change"; key: Name; path: Name | null }
    | { op: "expression.enable"; enabled: boolean; transit_out?: number; transit_in?: number }
    | ({ op: "model.configuration" } & (
        | { reset: true; event_files?: Name[]; radius?: never; somato?: never;
            effect_adaptations?: never; graph_emotion?: never }
        | { reset?: false; radius?: number; somato?: Integer; effect_adaptations?: EffectAdaptation[];
            graph_emotion?: Name | null; event_files?: Name[] }
    ))
    | { op: "expression.material"; cheek: number; shy: number; shadow: number; duration?: number }
    | { op: "model.scale"; scale: number }
    | { op: "model.follow_scale"; target: Target | null }
    | ({ op: "model.character_scale"; scale: number; outline: number; save_targets?: Name;
        multiplier?: [number, number][] | null; equal_ratio_by_min?: boolean } & (
        | { targets: Target[]; target_collection?: never; read_targets?: never }
        | { target_collection: Name; targets?: never; read_targets?: never }
        | { read_targets: Name; targets?: never; target_collection?: never }
    ));
export interface EffectAdaptation {
    path: Name;
    alias?: string | null;
    attach_point: string;
    position: Vec3;
    /** Euler angles. */
    rotation: Vec3;
    scale: number;
}
export type BgmSync = { op: "bgm.sync" } & (
    | { active: false }
    | { active?: true; state: Name; bpm: number; chapters: NonEmpty<BgmChapter> }
);
export interface BgmChapter {
    cue: string;
    phases: BgmPhase[];
    /** Omit or use null for a native null reference; [] supplies an empty phase list. */
    double_speed_phases?: BgmPhase[] | null;
}
export interface BgmPhase { start: number; blend: number; offset: number }
