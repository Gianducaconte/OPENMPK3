/* OPENMPK3 — chord styles, built diatonically from each style's scale (degrees are 0-based: 0 = I/i, 4 = V/v). */
#include "packs.h"
#include "theory.h"
#include <string.h>

const char *const FLAVOUR_NAMES[F_COUNT] = { "Triads", "7ths", "9ths", "Add9", "Sus2", "Sus4", "6ths", "11ths", "Power", "4ths" };

/* scale steps stacked above the chord degree */
static const int8_t STACK[F_COUNT][6] = {
    [F_TRI]  = { 0, 2, 4, -1 },        [F_7]    = { 0, 2, 4, 6, -1 },     [F_9]   = { 0, 2, 4, 6, 8, -1 },
    [F_ADD9] = { 0, 2, 4, 8, -1 },     [F_SUS2] = { 0, 1, 4, -1 },        [F_SUS4] = { 0, 3, 4, -1 },
    [F_6]    = { 0, 2, 4, 5, -1 },     [F_11]   = { 0, 4, 6, 8, 10, -1 }, [F_5]   = { 0, 4, 7, -1 },
    [F_QRT]  = { 0, 3, 6, 9, -1 },
};

static const struct { int8_t iv[5]; uint8_t n; } XQ[] = {
    { { 0, 4, 7 }, 3 }, { { 0, 3, 7 }, 3 }, { { 0, 2, 7 }, 3 }, { { 0, 5, 7 }, 3 }, { { 0, 7, 12 }, 3 },
    { { 0, 4, 7, 11 }, 4 }, { { 0, 3, 7, 10 }, 4 }, { { 0, 4, 7, 10 }, 4 }, { { 0, 4, 7, 14 }, 4 }, { { 0, 3, 7, 14 }, 4 },
    { { 0, 5, 7, 10 }, 4 }, { { 0, 3, 6, 10 }, 4 }, { { 0, 3, 7, 9 }, 4 }, { { 0, 4, 7, 10, 14 }, 5 },
};

#define P(d, f) { d, F_##f }
#define X(semi, q) { semi, X_##q }
const pack_t PACKS[] = {
    /* pop */
    { "Pop 1",        SC_MAJOR,      { P(0,TRI), P(4,TRI), P(5,TRI), P(3,TRI), P(1,TRI), P(2,TRI), P(4,SUS4), P(0,ADD9) }, VOICE_SMOOTH },
    { "Pop 2",        SC_MAJOR,      { P(0,ADD9), P(3,7), P(5,7), P(4,SUS4), P(1,7), P(2,7), P(3,ADD9), P(4,TRI) }, VOICE_SMOOTH },
    { "Pop 3",        SC_MIXOLYDIAN, { P(0,TRI), P(6,TRI), P(3,TRI), P(4,TRI), P(1,TRI), P(5,TRI), P(0,SUS4), P(3,ADD9) }, VOICE_SMOOTH },
    { "Dance / EDM",  SC_MAJOR,      { P(5,TRI), P(3,TRI), P(0,TRI), P(4,TRI), P(1,TRI), P(2,TRI), P(4,SUS4), P(3,ADD9) }, VOICE_BASS },
    { "K-Pop Anime",  SC_MAJOR,      { P(3,7), P(4,TRI), P(2,7), P(5,TRI), P(1,7), P(4,SUS4), P(0,ADD9), P(2,TRI) }, VOICE_SMOOTH },
    /* emotional */
    { "Sad 1",        SC_MINOR,      { P(0,TRI), P(5,TRI), P(2,TRI), P(6,TRI), P(3,TRI), P(4,TRI), P(5,7), P(0,ADD9) }, VOICE_SMOOTH },
    { "Sad 2",        SC_MINOR,      { P(0,9), P(5,7), P(3,7), P(6,SUS4), P(2,7), P(4,7), P(1,7), P(6,TRI) }, VOICE_OPEN },
    { "Sad 3",        SC_MAJOR,      { P(5,7), P(3,7), P(0,ADD9), P(4,SUS4), P(2,7), P(1,9), P(3,6), P(4,TRI) }, VOICE_SMOOTH },
    { "Cinematic 1",  SC_MINOR,      { P(0,TRI), P(5,TRI), P(2,TRI), P(6,TRI), P(3,TRI), P(0,5), P(5,ADD9), P(3,ADD9) }, VOICE_WIDE },
    { "Cinematic 2",  SC_HARM_MINOR, { P(0,TRI), P(5,TRI), P(3,TRI), P(4,TRI), P(0,ADD9), P(5,7), P(3,ADD9), P(4,SUS4) }, VOICE_BASS },
    { "Cinematic 3",  SC_LYDIAN,     { P(0,ADD9), P(1,TRI), P(4,SUS2), P(2,TRI), P(5,ADD9), P(0,9), P(4,TRI), P(1,ADD9) }, VOICE_WIDE },
    { "Thriller",SC_HARM_MINOR, { P(0,TRI), P(0,7), P(6,7), P(5,TRI), P(3,TRI), P(4,7), P(1,7), P(5,7) }, VOICE_LOW },
    { "Ambient",      SC_LYDIAN,     { P(0,SUS2), P(4,SUS2), P(1,ADD9), P(5,ADD9), P(0,9), P(2,SUS4), P(6,QRT), P(0,QRT) }, VOICE_WIDE },
    /* jazz & soul */
    { "Jazz 1",       SC_MAJOR,      { P(1,9), P(4,9), P(0,9), P(5,9), P(2,7), P(3,9), P(6,7), P(4,7) }, VOICE_WIDE },
    { "Jazz 2",       SC_HARM_MINOR, { P(1,7), P(4,7), P(0,9), P(3,7), P(5,7), P(0,TRI), P(4,TRI), P(6,7) }, VOICE_OPEN },
    { "Jazz 3",       SC_MAJOR,      { P(0,7), P(1,7), P(2,7), P(3,7), P(4,7), P(5,9), P(0,6), P(3,9) }, VOICE_OPEN },
    { "Jazz 4",       SC_DORIAN,     { P(0,QRT), P(1,QRT), P(2,QRT), P(3,QRT), P(4,QRT), P(5,QRT), P(6,QRT), P(0,11) }, VOICE_WIDE },
    { "Bossa",        SC_MAJOR,      { P(0,6), P(5,7), P(1,9), P(4,9), P(2,7), P(3,9), P(1,7), P(0,9) }, VOICE_OPEN },
    { "Bebop",        SC_MAJOR,      { P(1,7), P(4,7), P(0,6), P(5,7), P(2,7), P(3,6), P(6,7), P(4,9) }, VOICE_OPEN },
    { "Neo Soul 1",   SC_MAJOR,      { P(0,9), P(2,9), P(5,11), P(1,9), P(4,11), P(3,9), P(3,7), P(5,9) }, VOICE_OPEN },
    { "Neo Soul 2",   SC_DORIAN,     { P(0,9), P(3,9), P(6,9), P(2,9), P(4,7), P(1,7), P(0,11), P(3,7) }, VOICE_OPEN },
    { "Gospel",       SC_MAJOR,      { P(0,9), P(2,7), P(5,9), P(1,9), P(4,11), P(3,9), P(3,6), P(0,ADD9) }, VOICE_BASS },
    { "Lo-Fi 1",      SC_MAJOR,      { P(0,7), P(5,9), P(1,9), P(4,9), P(3,9), P(2,7), P(5,7), P(4,SUS4) }, VOICE_OPEN },
    { "Lo-Fi 2",      SC_MINOR,      { P(0,9), P(2,7), P(5,7), P(4,7), P(3,9), P(6,6), P(1,7), P(0,7) }, VOICE_LOW },
    { "City Pop",     SC_MAJOR,      { P(3,7), P(2,7), P(5,7), P(4,9), P(0,9), P(1,9), P(3,9), P(2,9) }, VOICE_SMOOTH },
    { "Blues",        SC_DORIAN,     { P(0,7), P(3,7), P(4,7), P(0,9), P(3,9), P(6,TRI), P(2,7), P(0,11) }, VOICE_BASS },
    /* retro / electronic */
    { "Vaporwave 1",  SC_MAJOR,      { P(3,7), P(2,7), P(1,9), P(0,9), P(5,9), P(3,9), P(4,ADD9), P(2,9) }, VOICE_WIDE },
    { "Vaporwave 2",  SC_LYDIAN,     { P(0,9), P(1,9), P(4,9), P(2,9), P(5,9), P(0,7), P(6,7), P(1,TRI) }, VOICE_WIDE },
    { "Synthwave 1",  SC_MINOR,      { P(0,TRI), P(5,TRI), P(2,TRI), P(6,TRI), P(3,TRI), P(4,SUS4), P(0,5), P(5,ADD9) }, VOICE_BASS },
    { "Synthwave 2",  SC_HARM_MINOR, { P(0,ADD9), P(5,7), P(3,7), P(3,TRI), P(4,TRI), P(6,7), P(0,TRI), P(4,7) }, VOICE_SMOOTH },
    { "Synthwave 3",  SC_LYDIAN,     { P(0,TRI), P(1,TRI), P(5,TRI), P(2,TRI), P(0,9), P(4,ADD9), P(4,7), P(6,TRI) }, VOICE_HIGH },
    { "Techno 1",     SC_MINOR,      { P(0,7), P(3,7), P(4,7), P(5,7), P(2,7), P(6,7), P(0,9), P(0,11) }, VOICE_SMOOTH },
    { "Techno 2",     SC_DORIAN,     { P(0,9), P(0,11), P(3,9), P(4,9), P(6,SUS4), P(2,9), P(0,QRT), P(1,7) }, VOICE_LOW },
    { "House",        SC_DORIAN,     { P(0,7), P(3,9), P(0,9), P(6,TRI), P(2,7), P(4,7), P(3,7), P(1,7) }, VOICE_SMOOTH },
    { "Disco Funk",   SC_DORIAN,     { P(0,9), P(3,9), P(1,7), P(4,7), P(6,9), P(2,7), P(3,7), P(0,11) }, VOICE_HIGH },
    { "Amapiano",     SC_MAJOR,      { P(0,9), P(5,9), P(2,7), P(3,9), P(1,11), P(4,11), P(3,7), P(4,9) }, VOICE_LOW },
    { "Rave",         SC_PHRYGIAN,   { P(0,5), P(1,5), P(2,5), P(3,5), P(5,5), P(6,5), P(0,TRI), P(1,TRI) }, VOICE_BASS },
    { "Metal",        SC_PHRYGIAN,   { P(0,5), P(1,5), P(2,5), P(3,5), P(4,5), P(5,5), P(6,5), P(0,TRI) }, VOICE_LOW },
    { "Trap Dark",    SC_HARM_MINOR, { P(0,TRI), P(5,TRI), P(4,TRI), P(3,TRI), P(0,7), P(6,7), P(1,TRI), P(5,7) }, VOICE_LOW },
    /* world */
    { "Napoli 1",     SC_HARM_MINOR, { P(0,TRI), P(3,TRI), P(4,7), P(5,TRI), P(1,7), P(3,7), P(4,TRI), P(0,ADD9) }, VOICE_BASS },
    { "Napoli 2",     SC_NEAP_MINOR, { P(0,TRI), P(1,TRI), P(3,TRI), P(1,7), P(5,TRI), P(0,SUS4), P(3,7), P(5,7) }, VOICE_SMOOTH },
    { "Flamenco",     SC_HIJAZ,      { P(0,TRI), P(1,TRI), P(3,TRI), P(6,TRI), P(0,7), P(1,7), P(3,7), P(0,SUS4) }, VOICE_OPEN },
    { "Middle East",  SC_HIJAZ,      { P(0,TRI), P(1,TRI), P(3,TRI), P(6,TRI), P(0,SUS4), P(1,7), P(0,5), P(3,ADD9) }, VOICE_SMOOTH },
    { "Indian 1",     SC_BHAIRAV,    { P(0,TRI), P(1,TRI), P(3,TRI), P(2,TRI), P(0,5), P(0,SUS4), P(1,7), P(0,7) }, VOICE_BASS },
    { "Indian 2",     SC_DORIAN,     { P(0,TRI), P(3,TRI), P(6,TRI), P(4,TRI), P(2,TRI), P(0,SUS2), P(1,TRI), P(0,5) }, VOICE_SMOOTH },
    { "Japan",        SC_HIRAJOSHI,  { X(0,MIN), X(8,MAJ), X(8,MAJ7), X(7,SUS4), X(0,SUS2), X(0,MADD9), X(7,5), X(0,5) }, VOICE_WIDE },
    { "Japan In",     SC_JAPANESE,   { X(5,MIN), X(1,MAJ), X(1,MAJ7), X(0,SUS4), X(5,MADD9), X(5,SUS2), X(0,5), X(5,5) }, VOICE_WIDE },
    { "Bali Gamelan", SC_PELOG,      { X(0,MIN), X(8,MAJ), X(8,MAJ7), X(8,SUS4), X(1,SUS2), X(0,5), X(8,5), X(1,5) }, VOICE_HIGH },
    { "Afrobeat",     SC_DORIAN,     { P(0,7), P(3,9), P(0,9), P(3,7), P(6,TRI), P(2,TRI), P(4,7), P(1,7) }, VOICE_SMOOTH },
    { "Highlife",     SC_DOM_PENTA,  { X(0,MAJ), X(7,MIN), X(0,7), X(0,SUS2), X(7,SUS4), X(0,ADD9), X(4,M7B5), X(0,9) }, VOICE_HIGH },
    { "Ethio Jazz",   SC_EGYPTIAN,   { X(7,MIN), X(10,MAJ), X(0,SUS4), X(5,SUS2), X(7,M7), X(0,7SUS4), X(0,SUS2), X(10,ADD9) }, VOICE_OPEN },
    { "Celtic",       SC_MIXOLYDIAN, { P(0,TRI), P(6,TRI), P(3,TRI), P(4,TRI), P(1,TRI), P(0,5), P(0,SUS4), P(6,ADD9) }, VOICE_OPEN },
    { "Lydian Dream", SC_LYD_DOM,    { P(0,9), P(1,TRI), P(5,7), P(4,TRI), P(0,7), P(1,7), P(2,TRI), P(0,6) }, VOICE_WIDE },
    /* ---- more moods and variations ---- */
    { "Pop 4",        SC_MAJOR,      { P(0,ADD9), P(5,7), P(3,7), P(4,SUS4), P(2,7), P(1,9), P(3,ADD9), P(4,TRI) }, VOICE_SMOOTH },
    { "Ballad",       SC_MAJOR,      { P(3,7), P(4,TRI), P(2,7), P(5,7), P(1,7), P(4,SUS4), P(0,ADD9), P(3,ADD9) }, VOICE_SMOOTH },
    { "Romantic",     SC_MAJOR,      { P(0,TRI), P(5,TRI), P(1,TRI), P(4,7), P(3,TRI), P(2,TRI), P(1,7), P(0,ADD9) }, VOICE_BASS },
    { "Lullaby",      SC_MAJOR,      { P(0,TRI), P(3,TRI), P(4,TRI), P(5,TRI), P(0,ADD9), P(3,ADD9), P(4,SUS4), P(0,6) }, VOICE_HIGH },
    { "Country",      SC_MAJOR,      { P(0,TRI), P(3,TRI), P(4,TRI), P(5,TRI), P(0,6), P(3,6), P(4,7), P(1,TRI) }, VOICE_SMOOTH },
    { "Reggae",       SC_MAJOR,      { P(0,TRI), P(3,TRI), P(4,TRI), P(5,TRI), P(1,TRI), P(0,6), P(3,6), P(4,7) }, VOICE_HIGH },
    { "Tropical",     SC_MAJOR,      { P(5,7), P(3,ADD9), P(0,ADD9), P(4,ADD9), P(1,9), P(2,7), P(3,9), P(5,ADD9) }, VOICE_SMOOTH },
    { "Hyperpop",     SC_MAJOR,      { P(0,TRI), P(4,ADD9), P(5,TRI), P(3,ADD9), P(0,SUS2), P(4,SUS4), P(2,TRI), P(3,TRI) }, VOICE_HIGH },
    { "8-Bit Game",   SC_MAJOR,      { P(0,TRI), P(3,TRI), P(4,TRI), P(5,TRI), P(0,5), P(4,5), P(3,SUS2), P(4,7) }, VOICE_HIGH },
    { "Sad 4",        SC_MINOR,      { P(0,ADD9), P(5,9), P(3,9), P(4,7), P(2,ADD9), P(6,ADD9), P(3,7), P(0,9) }, VOICE_OPEN },
    { "Cinematic 4",  SC_MIXO_B6,    { P(0,TRI), P(3,TRI), P(6,TRI), P(4,TRI), P(0,ADD9), P(3,ADD9), P(6,ADD9), P(0,5) }, VOICE_WIDE },
    { "Heroic",       SC_LYDIAN,     { P(0,TRI), P(1,TRI), P(4,TRI), P(5,TRI), P(0,ADD9), P(1,ADD9), P(4,SUS2), P(2,TRI) }, VOICE_BASS },
    { "Fantasy",      SC_DORIAN,     { P(0,TRI), P(3,TRI), P(6,TRI), P(2,TRI), P(4,TRI), P(0,ADD9), P(3,ADD9), P(6,ADD9) }, VOICE_WIDE },
    { "Fairytale",    SC_HARM_MAJOR, { P(0,7), P(3,7), P(5,7), P(4,7), P(1,7), P(0,ADD9), P(3,TRI), P(4,TRI) }, VOICE_OPEN },
    { "Horror",       SC_HUNG_MINOR, { P(0,TRI), P(0,7), P(5,TRI), P(4,TRI), P(6,TRI), P(4,7), P(5,7), P(0,ADD9) }, VOICE_LOW },
    { "Spy Movie",    SC_DORIAN_B2,  { P(0,TRI), P(1,TRI), P(3,TRI), P(6,TRI), P(0,7), P(1,7), P(3,7), P(2,TRI) }, VOICE_LOW },
    { "Noir",         SC_HARM_MINOR, { P(0,7), P(3,9), P(5,7), P(4,7), P(1,7), P(0,9), P(6,7), P(3,7) }, VOICE_LOW },
    { "Sci-Fi",       SC_LYD_AUG,    { P(0,7), P(1,TRI), P(1,9), P(5,TRI), P(6,TRI), P(2,TRI), P(0,QRT), P(5,7) }, VOICE_WIDE },
    { "Dream",        SC_WHOLE_TONE, { P(0,TRI), P(1,TRI), P(2,TRI), P(0,7), P(3,TRI), P(1,7), P(4,TRI), P(0,QRT) }, VOICE_WIDE },
    { "Ethereal",     SC_LYDIAN,     { P(0,SUS2), P(4,SUS2), P(1,SUS2), P(5,SUS2), P(0,QRT), P(2,QRT), P(4,QRT), P(1,ADD9) }, VOICE_WIDE },
    { "Post-Rock",    SC_MAJOR,      { P(0,ADD9), P(5,ADD9), P(3,ADD9), P(4,ADD9), P(0,SUS2), P(3,SUS2), P(5,SUS2), P(4,SUS4) }, VOICE_WIDE },
    { "Shoegaze",     SC_LYDIAN,     { P(0,ADD9), P(1,ADD9), P(5,ADD9), P(4,ADD9), P(0,SUS2), P(2,SUS4), P(1,SUS2), P(6,SUS4) }, VOICE_WIDE },
    { "Math Rock",    SC_LYDIAN,     { P(0,9), P(1,9), P(6,7), P(4,9), P(5,9), P(0,6), P(1,7), P(2,7) }, VOICE_OPEN },
    { "Punk",         SC_MIXOLYDIAN, { P(0,5), P(6,5), P(3,5), P(4,5), P(1,5), P(5,5), P(0,TRI), P(3,TRI) }, VOICE_BASS },
    { "Jazz 5",       SC_MAJOR,      { P(0,9), P(5,11), P(1,11), P(4,11), P(2,9), P(3,9), P(5,9), P(0,6) }, VOICE_WIDE },
    { "Jazz 6",       SC_MEL_MINOR,  { P(0,7), P(1,7), P(3,7), P(4,7), P(5,7), P(0,9), P(3,9), P(2,7) }, VOICE_OPEN },
    { "Bossa 2",      SC_MAJOR,      { P(1,9), P(4,11), P(0,9), P(5,9), P(2,7), P(3,9), P(1,7), P(0,6) }, VOICE_OPEN },
    { "Latin",        SC_DORIAN,     { P(0,7), P(3,7), P(0,9), P(3,9), P(4,7), P(6,TRI), P(1,7), P(0,6) }, VOICE_SMOOTH },
    { "Tango",        SC_HARM_MINOR, { P(0,TRI), P(3,TRI), P(4,7), P(5,TRI), P(1,7), P(0,5), P(4,TRI), P(3,7) }, VOICE_BASS },
    { "Neo Soul 3",   SC_MAJOR,      { P(3,9), P(2,11), P(5,11), P(1,11), P(0,9), P(4,11), P(2,9), P(3,6) }, VOICE_OPEN },
    { "R&B",          SC_MINOR,      { P(0,9), P(3,9), P(6,9), P(2,9), P(5,9), P(4,9), P(0,11), P(3,11) }, VOICE_OPEN },
    { "Soul 70s",     SC_MIXOLYDIAN, { P(0,7), P(3,7), P(4,7), P(0,9), P(1,7), P(6,7), P(3,6), P(0,6) }, VOICE_BASS },
    { "Funk",         SC_DORIAN,     { P(0,7), P(3,9), P(0,11), P(3,7), P(6,TRI), P(4,7), P(0,9), P(3,11) }, VOICE_HIGH },
    { "Gospel 2",     SC_MAJOR,      { P(0,9), P(3,9), P(2,7), P(5,9), P(1,11), P(4,11), P(3,6), P(0,ADD9) }, VOICE_BASS },
    { "Lo-Fi 3",      SC_DORIAN,     { P(0,9), P(3,9), P(4,7), P(6,9), P(2,9), P(1,11), P(0,11), P(3,7) }, VOICE_OPEN },
    { "Chill Hop",    SC_MAJOR,      { P(3,9), P(2,9), P(5,9), P(1,9), P(0,9), P(4,11), P(3,6), P(5,7) }, VOICE_OPEN },
    { "City Pop 2",   SC_MAJOR,      { P(3,7), P(4,11), P(2,7), P(5,9), P(1,9), P(4,9), P(0,9), P(3,9) }, VOICE_SMOOTH },
    { "Vaporwave 3",  SC_MAJOR,      { P(1,9), P(4,11), P(0,9), P(5,9), P(3,9), P(2,9), P(1,7), P(0,7) }, VOICE_WIDE },
    { "Synthwave 4",  SC_MINOR,      { P(0,7), P(5,7), P(6,TRI), P(3,7), P(2,7), P(4,7), P(0,ADD9), P(5,ADD9) }, VOICE_SMOOTH },
    { "Techno 3",     SC_MINOR,      { P(0,9), P(0,7), P(5,7), P(3,7), P(0,11), P(4,7), P(2,7), P(6,7) }, VOICE_SMOOTH },
    { "House 2",      SC_MAJOR,      { P(3,9), P(2,7), P(5,9), P(1,11), P(0,9), P(4,11), P(3,7), P(5,11) }, VOICE_OPEN },
    { "Afro House",   SC_DORIAN,     { P(0,9), P(0,11), P(3,9), P(4,11), P(6,ADD9), P(2,9), P(0,7), P(3,7) }, VOICE_SMOOTH },
    { "UK Garage",    SC_MINOR,      { P(0,9), P(5,9), P(3,9), P(4,9), P(2,9), P(6,9), P(0,7), P(3,11) }, VOICE_OPEN },
    { "Drum & Bass",  SC_MINOR,      { P(0,9), P(5,9), P(2,9), P(6,9), P(3,11), P(0,11), P(5,7), P(4,9) }, VOICE_WIDE },
    { "Dubstep",      SC_PHRYGIAN,   { P(0,TRI), P(1,TRI), P(0,5), P(1,5), P(3,TRI), P(5,TRI), P(6,TRI), P(3,5) }, VOICE_LOW },
    { "Klezmer",      SC_UKR_DORIAN, { P(0,TRI), P(1,TRI), P(1,7), P(4,TRI), P(0,7), P(0,ADD9), P(2,TRI), P(4,5) }, VOICE_OPEN },
    { "Balkan",       SC_HIJAZ,      { P(0,TRI), P(1,TRI), P(3,TRI), P(6,TRI), P(0,7), P(3,7), P(1,7), P(0,SUS4) }, VOICE_BASS },
    { "Bollywood",    SC_HARM_MINOR, { P(0,TRI), P(5,TRI), P(3,TRI), P(4,TRI), P(6,7), P(0,ADD9), P(5,7), P(3,ADD9) }, VOICE_SMOOTH },
    { "Celtic 2",     SC_DORIAN,     { P(0,TRI), P(6,TRI), P(3,TRI), P(0,5), P(4,TRI), P(2,TRI), P(3,ADD9), P(0,SUS2) }, VOICE_OPEN },
    { "China",        SC_MAJ_PENTA,  { X(0,MAJ), X(9,MIN), X(7,SUS2), X(0,SUS2), X(2,SUS4), X(9,M7), X(0,ADD9), X(7,5) }, VOICE_HIGH },
};

/* voicing preset per plain scale (K2 scale entries) */
const uint8_t SCALE_VOICING[SCALE_COUNT] = {
    [SC_LYD_DOM] = VOICE_WIDE, [SC_BEBOP] = VOICE_OPEN, [SC_BEBOP_MIN] = VOICE_OPEN, [SC_MEL_MINOR] = VOICE_OPEN,
    [SC_LYD_AUG] = VOICE_WIDE, [SC_WHOLE_TONE] = VOICE_WIDE, [SC_HARM_MAJOR] = VOICE_OPEN, [SC_MIXO_B6] = VOICE_WIDE,
    [SC_HUNG_MINOR] = VOICE_LOW, [SC_DORIAN_B2] = VOICE_LOW, [SC_PHRYGIAN] = VOICE_LOW, [SC_LYDIAN] = VOICE_WIDE,
    [SC_JAPANESE] = VOICE_WIDE, [SC_HIRAJOSHI] = VOICE_WIDE, [SC_PELOG] = VOICE_HIGH, [SC_SLENDRO] = VOICE_HIGH,
};
const int PACK_COUNT = (int)(sizeof PACKS / sizeof PACKS[0]);

int scale_chord(int scale, int root, int deg, int fl, int voicing, uint8_t *out, char *name)
{
    if (fl < 0 || fl >= F_COUNT) fl = F_TRI;
    int base = scale_degree_offset((uint8_t)scale, deg);
    int8_t iv[6]; int n = 0;
    for (int i = 0; i < 6 && STACK[fl][i] >= 0; i++) {
        int s = scale_degree_offset((uint8_t)scale, deg + STACK[fl][i]) - base;
        if (n && s <= iv[n - 1]) s += 12;               /* keep stacking upward */
        iv[n++] = (int8_t)s;
    }
    int croot = ((root + base) % 12 + 12) % 12;
    if (name) {
        uint8_t rp[6];                                  /* name from root position */
        for (int i = 0; i < n; i++) rp[i] = (uint8_t)(48 + croot + iv[i]);
        chord_name(rp, n, name);
        if (fl == F_QRT) { strcpy(name, NOTE_NAMES[croot]); strcat(name, " 4ths"); }
    }
    return voice_chord(croot, iv, n, voicing, out);
}

int pack_chord(int pack, int pad, int root, int voicing, uint8_t *out, char *name)
{
    const pack_chord_t *c = &PACKS[pack].ch[pad & 7];
    if (c->fl < 0x80) return scale_chord(PACKS[pack].scale, root, c->deg, c->fl, voicing, out, name);
    int q = c->fl - 0x80, croot = (root + c->deg) % 12;
    if (name) {
        uint8_t rp[5];
        for (int i = 0; i < XQ[q].n; i++) rp[i] = (uint8_t)(48 + croot + XQ[q].iv[i]);
        chord_name(rp, XQ[q].n, name);
    }
    return voice_chord(croot, XQ[q].iv, XQ[q].n, voicing, out);
}
