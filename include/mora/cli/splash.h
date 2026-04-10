#pragma once

#include <cstdlib>
#include <ctime>
#include <string_view>

namespace mora {

constexpr std::string_view MORA_BANNER = R"(
   ███╗   ███╗ ██████╗ ██████╗  █████╗
   ████╗ ████║██╔═══██╗██╔══██╗██╔══██╗
   ██╔████╔██║██║   ██║██████╔╝███████║
   ██║╚██╔╝██║██║   ██║██╔══██╗██╔══██║
   ██║ ╚═╝ ██║╚██████╔╝██║  ██║██║  ██║
   ╚═╝     ╚═╝ ╚═════╝ ╚═╝  ╚═╝╚═╝  ╚═╝)";

// Rotating taglines — selected randomly at startup
constexpr const char* SPLASH_TEXTS[] = {
    // ── Hermaeus Mora ──────────────────────────────────────
    "The all-knowing patcher.",
    "All seekers of knowledge come to me, sooner or later.",
    "This is Apocrypha, where all patches are hoarded.",
    "Here then is the knowledge you need, although you did not know you needed it.",
    "Knowledge for knowledge. Patches for patches.",
    "You could spend a hundred lifetimes searching my library.",
    "All that is known shall be indexed.",
    "Come, my champion. Your load order awaits.",
    "Most impressive.",
    "Your free will is an illusion. Your load order is not.",
    "You are Dragonborn. You also seek to bend the world to your will.",
    "All knowledge shall be gathered. All patches shall be frozen.",
    "I am the answer to all questions and the question behind every answer.",
    "Be warned. Many have thought as you do. I have broken them all.",
    "Your pathetic mortal patcher amuses me.",
    "Well done, my champion. Your journey towards enlightenment has led you here.",
    "My word is as true as fate, as inevitable as destiny.",
    "You thought to reject me, and yet here you are.",
    "Did you think to escape me? You can hide nothing from me here.",
    "At last, the mod authors yield up their secrets to me.",

    // ── Sheogorath ─────────────────────────────────────────
    "Cheese for everyone! Wait, scratch that. Patches for everyone!",
    "Is it just me or is it getting faster and faster these days?",
    "I'm a mad patcher. The Mad Patcher, actually. It's a family title.",
    "Do you mind? I'm busy doing the fishstick. It's a very delicate state of mind!",
    "You know, you remind me of myself at a young age. All I cared about was compiling...",
    "Fixed is such a subjective term. I think 'patched' is far more appropriate.",
    "Yes, yes, you're entirely brilliant. Conquering load orders and all that. Blah blah blah.",
    "I once dug a pit and filled it with patches... or was it conflicts...",
    "Change will preserve us! It will move mountains! It will mount movements!",
    "Ha! I do love it when the mortals know they're being patched.",
    "Well? Spit it out, mortal. I haven't got an eternity! Actually... I do. Little joke.",

    // ── Paarthurnax ────────────────────────────────────────
    "What is better: to be born a fast patcher, or to overcome slow startup through great effort?",
    "The curse of much knowledge is often indecision. But not for Datalog.",
    "Patience. I am compiling in my way.",
    "There is no distinction between patching and combat to a dragon.",
    "Drem Yol Lok. Greetings, Dragonborn. Your patches are ready.",

    // ── Guards ─────────────────────────────────────────────
    "I used to be a patcher like you, then I took a Datalog to the knee.",
    "Let me guess... someone stole your load order?",
    "No lollygaggin'. Compile your patches.",
    "Watch the skies, traveler. And your load order.",
    "Iron sword, huh? What are you patching, butterflies?",
    "Must've been my imagination. — every patcher, on zero conflicts",
    "Psst... Hail Mora.",
    "By order of the Jarl, stop right there! Your patches are ready.",
    "I'd be a lot warmer and a lot happier with a belly full of pre-computed patches.",

    // ── Nazeem & other NPCs ────────────────────────────────
    "Do you get to the compile phase very often? Oh, what am I saying.",
    "Everything's for sale, my friend. Everything. If I had a patcher I'd sell it in a second.",
    "These sands are cold, but Khajiit feels warmness from your patches.",
    "Khajiit has patches if you have coin.",

    // ── Cicero ─────────────────────────────────────────────
    "Madness is merry, and merriment's might, when the patcher comes calling with patches at night.",
    "Just stab stab stab stab! — Cicero, on field-level patching",
    "Best friends forever. — your patches and your save file",

    // ── M'aiq the Liar ────────────────────────────────────
    "M'aiq knows much about patching, and tells some.",
    "Some say runtime patching is the way. M'aiq says they are liars.",
    "M'aiq has heard of these 'SKSE plugins'. They've got all the DLLs. DLLs taking over everything.",

    // ── Other Daedric Princes ──────────────────────────────
    "A NEW HAND TOUCHES THE PATCHER. — Meridia, probably",
    "Weak. He's weak. You're strong. Compile him. — Molag Bal",
    "The Oath has been struck, the die has been cast, and your patches await you.",
    "Fear not. You'll have your patches, your desire for power, your hunger for performance.",

    // ── Skyrim memes / community humor ─────────────────────
    "Fus Ro Patch!",
    "Hey you, you're finally loading... faster.",
    "Never should have come here. — every replaced SKSE plugin",
    "I am sworn to carry your patches. — Lydia",
    "My cousin's out fighting dragons, and what do I get? Merge conflicts.",
    "I mostly deal with petty thievery and drunken brawls. Been too long since we've had a good patch.",
    "Skyrim belongs to the Nords! And its patches belong to Mora.",

    // ── Technical humor ────────────────────────────────────
    "Precalculated for your convenience.",
    "Making Wabbajack lists boot before heat death.",
    "Your ENB still takes longer.",
    "Tested on a 2,000 mod list. It lived.",
    "Runtime is a suggestion, not a requirement.",
    "O(1) lookup, O(my god) it's fast.",
    "Field-level merging. You're welcome.",
    "Your patches are now diamonds. Frozen diamonds.",
    "Bottom-up evaluation, top-tier results.",
    "Facts don't care about your load order. Well, actually they do.",
    "Memoized, optimized, and Dovahkiin-approved.",
    "70 patchers walk into a bar. One walks out.",
    "One language. Every patch. Zero startup cost.",
    "Now with 100% fewer SKSE DLLs.",
    "This isn't even my final form. — Phase 1",

    // ── Loading screen parodies ────────────────────────────
    "When a patcher uses a compile attack, it is speaking in an ancient and powerful language.",
    "The patch skill makes it more difficult for conflicts to be detected.",
    "Dual patching potentially doubles performance... but prevents any kind of runtime.",
    "Skyrim legend tells of a tool known as the Mora, with the body of Datalog and the soul of a dragon.",

    // ── Deep lore cuts ─────────────────────────────────────
    "The Dwemer were the first to pre-compute their patches. Look how that turned out.",
    "Like an Elder Scroll, it does not exist, but it has always existed.",
    "Su'um ahrk morah. — breath and focus, the patcher's way",
    "In the tongue of the Dov: MOR AH. Compile and seek.",
    "The Black Books contain knowledge mortals were never meant to have. So does mora.patch.",
    "Apocrypha's stacks are infinite. Your load times are not. You're welcome.",
    "Even Neloth would approve of this level of optimization.",
    "Serana says your load order could use some work.",
    "Alduin may have eaten the world, but he couldn't eat your compile time.",
    "The Greybeards meditated for years. You compiled in milliseconds.",
};

constexpr size_t SPLASH_COUNT = sizeof(SPLASH_TEXTS) / sizeof(SPLASH_TEXTS[0]);

inline const char* random_splash() {
    static bool seeded = false;
    if (!seeded) {
        std::srand(static_cast<unsigned>(std::time(nullptr)));
        seeded = true;
    }
    return SPLASH_TEXTS[std::rand() % SPLASH_COUNT];
}

} // namespace mora
