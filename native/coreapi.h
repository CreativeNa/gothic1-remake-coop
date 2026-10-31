/* Interface between the core DLL (coopdll: hook, net, puppet) and hot-reloadable modules (modules\<name>.dll).
 * The core stays loaded; a module can be rebuilt and is reloaded by the core without restarting the game. */
#ifndef COREAPI_H
#define COREAPI_H
#include <stdint.h>
#include <stddef.h>

#define COREAPI_VERSION 11

typedef struct CoreAPI {
    int version;

    /* logging (writes to coopdll.log) */
    void (*log)(const char *fmt, ...);

    /* what the other player is doing (refreshed by the core every frame, read-only for modules) */
    int have_remote;
    double speed;               /* horizontal speed in u/s (measured on the sender's CharacterMovement) */
    double vz;                  /* vertical speed in u/s */
    int movemode;               /* remote CharacterMovement mode: 1 walking, 3 falling */
    int crouched;               /* remote bIsCrouched */
    const char *action;         /* AnimSequence name of the remote montage, or "-" */
    const char *action_pkg;     /* its package path, or "-" */
    unsigned long fall_since;   /* GetTickCount() when movemode last became 3 */

    /* game objects (game thread only, validate before use) */
    uint8_t *hero;              /* local hero actor */
    uint8_t *puppet;            /* the other player's puppet actor */
    uint8_t *puppet_mesh;       /* its SkeletalMeshComponent */

    /* helpers (all game-thread only) */
    uint8_t *(*find_anim)(const char *name);                       /* loaded AnimSequence by name */
    uint8_t *(*clip_by_name)(const char *name, const char *pkg);   /* find, or load by package path; rooted + cached */
    int      (*play_seq)(uint8_t *seq, int loop);                  /* play on the puppet mesh */
    int      (*play_named)(const char *name, int loop);
    void     (*freeze_at_end)(uint8_t *seq);                       /* play once, stop on the last frame */
    double   (*seq_length)(uint8_t *seq);                          /* seconds, 0 if unknown */
    int      (*obj_alive)(uint8_t *obj, const char *name);
    void     (*root_obj)(uint8_t *obj);
    void     (*list_anims)(const char *substring, int max);        /* log loaded AnimSequences containing substring */
    void     (*dump_props)(uint8_t *obj, const char *tag, int max);/* log an object's properties (recon) */
    int      (*find_prop_offset)(uint8_t *ustruct, const char *name);
    uint8_t *(*obj_class)(uint8_t *obj);
    void     (*obj_name)(uint8_t *obj, char *out, int cap);
    int      (*read_mem)(const void *src, void *dst, size_t n);    /* guarded read, 1 = ok */
    int      (*read_bool)(uint8_t *obj, const char *prop);         /* bitfield bool by property name */

    /* equipment on the puppet */
    uint8_t *(*find_object)(const char *cls_name, const char *name, const char *outer_name);
    uint8_t *(*load_asset)(const char *package_path, const char *asset_name);   /* LoadAsset_Blocking by path */
    uint8_t *(*spawn_actor)(uint8_t *uclass);                                   /* at the puppet's position, rooted */
    int      (*attach_to_puppet)(uint8_t *actor, const char *socket);           /* actor root -> puppet mesh socket, snap */
    void     (*destroy_actor)(uint8_t *actor);

    /* call a UFunction with plain int/bool parameters (in declaration order); *ret receives the int/bool result */
    int  (*call_int)(uint8_t *obj, const char *outer_class, const char *function, int nargs, const int *args, int *ret);
    /* AnimSequences loaded since the previous call (the first call only records a baseline) */
    void (*scan_new_anims)(void (*cb)(const char *name, const char *package, void *ud), void *ud);

    /* iterate every UObject (game thread only); object_at may return NULL */
    int      (*num_objects)(void);
    uint8_t *(*object_at)(int index);
    void     (*fname_to_str)(const void *fname_ptr, char *out, int cap);   /* an FName stored at that address */

    /* free-form text sent with every packet: modules describe local state, the other side reads it */
    void (*set_local_extra)(const char *text);
    const char *remote_extra;                                                   /* latest text from the other player */

    /* v7: generic UFunction call with named parameters. sizes[i] = bytes copied from vals[i], or -1 when vals[i] is a char* turned into an FName.
     * sizes[i] = -2: vals[i] holds the raw 8 bytes of an FName (e.g. taken from a GetBoneName result), copied as they are.
     * The ReturnValue (if any) is copied to ret (at most retcap bytes). Returns 1 if the call ran. */
    int  (*call_fn)(uint8_t *obj, const char *outer, const char *fn, int n, const char **names, const void **vals, const int *sizes, void *ret, int retcap);
    /* v7: a binary datagram sent next to the normal packet every 3 frames (bone poses); the other side's latest one is remote_blob */
    void (*set_local_blob)(const void *data, int len);
    const uint8_t *remote_blob; int remote_blob_len; int remote_blob_seq;       /* seq grows with every new blob */
    /* v7: component that equipment is attached to instead of the puppet's skeletal mesh (NULL = default); attach_gen changes with it */
    void (*set_attach_target)(uint8_t *comp);
    int attach_gen;

    /* v8: live recon helpers for the probe module */
    /* every property and function of a class and its parents: kind "prop" or "fn", the struct that declares it, name, property type name, offset, size */
    void (*list_members)(uint8_t *ustruct, void (*cb)(const char *kind, const char *sname, const char *name, const char *type, int off, int size, void *ud), void *ud);
    /* parameters of a UFunction without calling it, e.g. "NewRate@0/4 ReturnValue@4/1"; returns 0 if the function does not exist */
    int  (*describe_fn)(const char *outer, const char *fn, char *out, int cap);
    int  (*write_mem)(void *dst, const void *src, size_t n);                      /* guarded write, 1 = ok */

    /* v9: second binary channel (shared world: NPC states), same rules as the first one */
    void (*set_local_blob2)(const void *data, int len);
    const uint8_t *remote_blob2; int remote_blob2_len; int remote_blob2_seq;

    /* v10: third channel (story flags, quest states); a blob set here is sent once */
    void (*set_local_blob3)(const void *data, int len);
    const uint8_t *remote_blob3; int remote_blob3_len; int remote_blob3_seq;

    /* v11: function observer. observe_fn swaps the native body of a UFunction for a recorder (returns a slot, -1 if the function has no body of its own);
     * obs_pop takes the oldest recorded call: slot, the object it was called on, the parameter buffer (only if the call came through ProcessEvent: had_locals) */
    int (*observe_fn)(const char *outer, const char *fn);
    int (*obs_pop)(int *slot, uint8_t **obj, uint8_t *params, int cap, int *plen, int *had_locals);
} CoreAPI;

/* exports of a module */
typedef void (*ModInit_t)(CoreAPI *api);
typedef void (*ModUpdate_t)(CoreAPI *api);     /* once per engine frame, while the puppet exists */
typedef void (*ModShutdown_t)(CoreAPI *api);

#endif
