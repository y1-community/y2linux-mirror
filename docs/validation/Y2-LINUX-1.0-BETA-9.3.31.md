# Y2Linux 1.0 Beta 9.3.31: the "End of Song" sleep timer no longer stops the music you choose

Release `1.0.0-beta.9.3.31`, Y2Reborn `1.0.0-beta.9.3.31`, kernel `6.18.56-y2linux-1.0.0-beta.9.3.31`, update
sequence 16800, root `2026.08-platform-v1.285`. Published as GitHub release `baseline-262` (build 262). Previous
release: [Beta 9.3.30](Y2-LINUX-1.0-BETA-9.3.30.md).

This report is written for people who use the player. A short technical appendix is at the end.

## In one paragraph

A listener reported that after using the sleep timer's **End of Song** overnight, the player would not let them
skip songs or start one with Play the next morning ([Y2Reborn #137](https://github.com/SchulzCode/Y2Reborn/issues/137)).
The timer took any change of song, including the listener's own, for the end of the song it was set on, and paused
what they had just chosen. This release fixes that.

## What changed

- **The timer goes along with the song you choose.** With **End of Song** set, Next, Previous, a song picked from a
  list and a new queue no longer pause the song they start. While you listen, the timer carries on with the new song
  (or chapter): it still ends at the end of the song you are now hearing, so skipping on the way to sleep still ends
  in sleep.
- **A leftover timer ends.** If playback was paused or stopped and you then start another song, the old End of Song
  timer ends and the player says "Sleep timer off". Before, it paused the new song a moment after it began, and the
  player went back to the old song, which made Next and Play seem dead.
- **Adding or removing songs in the queue** no longer changes what the timer waits for: it follows the song itself,
  not its place in the queue.
- Timed sleep (5 minutes to 3 hours) is unchanged.

## How it was tested

- **Tests**: the platform's full check, Reborn's tests and the tests of the release images passed. New tests: the
  timer carrying on with a song chosen while playing, ending when a song is chosen after a pause or stop, being left
  alone when the listener did not change the song, following a new chapter of a book, and keeping its song when songs
  before it leave the queue.
- **On one player** (Beta 9.3.30): the problem was reproduced before the fix: with End of Song set, Next, and
  starting another song, paused it again within a second, and the new song did not start.
- **Not tested on the player**: this fix itself. Also unchanged and untested: End of Song while Repeat Song is on.

## Updating

Settings → System → Software Update → **Update Now**. Your music, playlists, settings, Wi-Fi networks and Bluetooth
pairings stay. **Restore Previous System** (Settings → System → About, tap Version seven times, then Diagnostics) goes
back to the version you had.

## Known issues

Everything listed for [Beta 9.3.30](Y2-LINUX-1.0-BETA-9.3.30.md#known-issues) still applies. The report of #137 also
asked why the timer was still set in the morning; that is not known yet (the error report `4001AB49` was not at hand).

## Technical appendix

- Reborn `crates/reborn-core/src/sleep.rs` (new; the sleep timer moved out of `lib.rs`): the timer's item is the
  queue entry id (`QueueEntryId`) and the chapter, not the queue position; `SleepTimer::after_listener_moved` carries
  an end-of-item timer to the new item while playing and ends it otherwise. `app/reborn/src/runtime/effects.rs`
  (`effect` wraps `run_effect`: every listener action goes through it; natural advances come from the decoder events
  and are still told apart by the timer's tick) and `sleep_timer.rs` (`sleep_timer_follows_listener`).
- Images built from Linux `59fc2156`, Reborn `200e56d`.
