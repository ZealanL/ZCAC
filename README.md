# ZCAC
ZCAC (Zealan's Compressed Audio Codec) is a custom audio compression algorithm/format I'm making for fun :D

It currently is in very early development/testing, and only compresses .WAV files.

Current compression results are about 5-10% of the original file size, with almost no noticable difference in audio quality.

**How ZCAC works (for now):**
- Load a .WAV file and determine the raw signal data for each audio channel
- Convert the raw signal data into Fourier-transformed segments (blocks)
- Reduce the size of the Fourier blocks by converting each floating point value to a small integer
- Compress and store the results