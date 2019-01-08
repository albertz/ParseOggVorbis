#  ParseOggVorbis

Documentation:
https://xiph.org/vorbis/doc/
https://xiph.org/vorbis/doc/Vorbis_I_spec.html
https://xiph.org/vorbis/doc/oggstream.html
https://xiph.org/vorbis/doc/framing.html
https://xiph.org/vorbis/doc/v-comment.html
https://github.com/xiph/vorbis/blob/master/lib/vorbisfile.c

* The first Vorbis packet (the identification header), which uniquely identifies a stream as Vorbis audio, is placed alone in the first page of the logical Ogg stream. This results in a first Ogg page of exactly 58 bytes at the very beginning of the logical stream.
* This first page is marked ’beginning of stream’ in the page flags.
* The second and third vorbis packets (comment and setup headers) may span one or more pages beginning on the second page of the logical stream. However many pages they span, the third header packet finishes the page on which it ends. The next (first audio) packet must begin on a fresh page.
* The granule position of these first pages containing only headers is zero.
* The first audio packet of the logical stream begins a fresh Ogg page.
* Packets are placed into ogg pages in order until the end of stream.
* The last page is marked ’end of stream’ in the page flags.
* Vorbis packets may span page boundaries.
* The granule position of pages containing Vorbis audio is in units of PCM audio samples (per channel; a stereo stream’s granule position does not increment at twice the speed of a mono stream).
* The granule position of a page represents the end PCM sample position of the last packet completed on that page. The ’last PCM sample’ is the last complete sample returned by decode, not an internal sample awaiting lapping with a subsequent block. A page that is entirely spanned by a single packet (that completes on a subsequent page) has no granule position, and the granule position is set to ’-1’.
  
  Note that the last decoded (fully lapped) PCM sample from a packet is not necessarily the middle sample from that block. If, eg, the current Vorbis packet encodes a ”long block” and the next Vorbis packet encodes a ”short block”, the last decodable sample from the current packet be at position (3*long_block_length/4) - (short_block_length/4).

* The granule (PCM) position of the first page need not indicate that the stream started at position zero. Although the granule position belongs to the last completed packet on the page and a valid granule position must be positive, by inference it may indicate that the PCM position of the beginning of audio is positive or negative.
    * A positive starting value simply indicates that this stream begins at some positive time offset, potentially within a larger program. This is a common case when connecting to the middle of broadcast stream.
    * A negative value indicates that output samples preceeding time zero should be discarded during decoding; this technique is used to allow sample-granularity editing of the stream start time of already-encoded Vorbis streams. The number of samples to be discarded must not exceed the overlap-add span of the first two audio packets.
  
  In both of these cases in which the initial audio PCM starting offset is nonzero, the second finished audio packet must flush the page on which it appears and the third packet begin a fresh page. This allows the decoder to always be able to perform PCM position adjustments before needing to return any PCM data from synthesis, resulting in correct positioning information without any aditional seeking logic.

  Note: Failure to do so should, at worst, cause a decoder implementation to return incorrect positioning information for seeking operations at the very beginning of the stream.

* A granule position on the final page in a stream that indicates less audio data than the final packet would normally return is used to end the stream on other than even frame boundaries. The difference between the actual available data returned and the declared amount indicates how many trailing samples to discard from the decoding process.

