<h2>Changes in Audacity 1.2</h2>

Major changes since our previous stable release, Audacity 1.0.0:

<h3>Professional-Quality Audio</h3>

<ul>
<li>
Audacity can now record and edit 24-bit and 32-bit (floating-point)
samples.  Tracks with different sample rates and formats can exist in
the same project, and Audacity will convert them in realtime whenever
necessary.  High-quality dithering and resampling is used for all
conversions.  
</li>

<li>
Audacity's sound input and output has been improved.  Audacity can now
record more than two channels at once.  Buffering is improved, for lower
chances of skipping and buffer underruns.
</li>
</ul>

<a href="screenshots.php<?php print $langQueryStr; ?>">
<img alt="screenshots" border="0" src="images/screen/linux/change-pitch.png" align="right"></a>
<h3>Effects</h3>

<ul>
<li>
Three new effects change the pitch and tempo of a track:
 <ul>
  <li>Change Pitch raises or lowers the tone of a selection, without
  affecting the speed.</li>
  <li>Change Tempo makes the selection play faster or slower, without
  altering the pitch.</li>
  <li>Change Speed alters both the playback speed and the pitch, just
  like changing the speed of a turntable or tape player.</li>
 </ul>
</li>

<li>
Most effects now include a "Preview" button, which allows you to try
different settings without closing the effect window.  A new command
allows you to repeat the last effect without re-opening the window.
</li>

<li>
Other new effects include:
 <ul>
  <li>Compressor, for dynamic range compression.</li>
  <li>Repeat, to loop samples.</li>
  <li>Normalize, to adjust volume and correct DC bias.</li>
 </ul>
</li>
</ul>


<h3>New Editing Features</h3>

<ul>
<li>
The Envelope tool, used for smoothly fading tracks in and out, can now be
used to make tracks louder than their original volume as well as quieter.
</li>

<li>
The new "Time track" feature is similar to the volume envelope, but
instead changes the playback speed smoothly as a track plays.
</li>

<li>
Each track now has its own Gain and Pan controls, for easier mixing.
<a href="screenshots.php<?php print $langQueryStr; ?>"><img alt="screenshots" border="0" src="images/screen/linux/track-controls.png" align="right"></a>
</li>

<li>
Audacity can find zero-crossings, to help create smooth cuts and loops.
Press "Z" to move selection edges to the nearest zero-crossings.
</li>
</ul>


<h3>Plugins</h3>

<ul>
<li>
On Linux, Audacity can now load <a href="http://www.ladspa.org/">LADSPA</a>
plugins.
</li>

<li>
Audacity 1.2 features a digital signal processing language called
<a href="nyquist.php">Nyquist</a>, which allows users to program new effects
in a LISP-like language.
</li>
</ul>


<h3>File Import and Export</h3>

<ul>
<li>
Audacity 1.2 project files use a new XML file format.  Audacity 1.2 will
automatically open and convert project files from earlier releases.
</li>

<li>
Audacity 1.2 uses <a href="http://www.underbit.com/products/mad/">libmad</a>
for much faster decoding of MP3 files.  Erik de Castro Lopo's
<a href="http://www.zip.com.au/~erikd/libsndfile/">libsndfile</a> provides
improved compatibility with many uncompressed audio file formats.
</li>

<li>
The import and open dialogs now allow Audacity users to select several
audio files at once, and open them into a single project.  A new "LOF"
file format provides a way for Audacity to open a group of files with
offsets listed in a text file.
</li>
</ul>


<h3>Improved User Interface</h3>

<a href="screenshots.php<?php print $langQueryStr; ?>"><img alt="screenshots" border="0" src="images/screen/macosx/main-toolbar.png" align="right"></a>
<ul>
<li>
New Edit and Mixer toolbars for fast access to common functions.
</li>

<li>
The new Drawing tool allows adjustment of individual samples, when fully
zoomed in.  The new Multi-Tool mode gives quick access to different editing
functions without needing to switch tools.
</li>

<li>
Many new keyboard commands have been added, and keyboard shortcuts can
now be customized.
</li>

<li>
New commands:
 <ul>
  <li>Looped play.  Type "L", or hold down shift when clicking Play.</li>
  <li>Type "1" to play a 1-second preview of the audio around the cursor.</li>
 </ul>
</li>

<li>
The mouse wheel can be used to zoom in and out.
</li>

<li>
Tracks can now be zoomed vertically by clicking or dragging in the
vertical rulers.  Shift-click or right-click to zoom out.  
</li>

<li>
The ruler and status bar can now display time in several different
formats, including seconds, samples, or video frames.
</li>

<li>
Audacity's interface can now be translated into languages other than
English.  You can volunteer to help <a href="translation/">translate
Audacity</a> into your native language.
</li>
</ul>
