// Spike step 1: does Inheritance Injection compile against the shipped script cache?
// Hooks UG1RPlayer exactly like the modkit docs (script-hooking.md). UNTESTED.
UCLASS(meta=(ReplaceParentClass))
class UG1RPlayer_CoopProbe : UG1RPlayer
{
}
