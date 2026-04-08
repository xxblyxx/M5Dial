# bugs

# ui updates

# completed
- when timer is counting down, it should not go to sleep
- simplify the alarm go'ing off screen; remove all animations except for the timer digits and just flash the screen black and white; we want the screen refresh to be quick so there's no delay in the beep sound
- the alarm sound only beeps once; this is completely different than ../timer/timer.ino, where the alarm beeps multiple times and flashes the screen
- use a complete black background
- main screen, the hrs, min, sec red pill boxes, remove the white border around the pill box
- the selected hrs, min, sec pill boxes turns purple when selected, with the green font color, it makes it hard to read, change the selected pill box color
- the count down progress bar is red with a white border, remove the white border
- the progress bar is now missing
- change the hrs, min, sec pill boxes back to red with no border color; don't change the color when selected stick with the underline that's already in place
- when the alarm is sounding, touching the screen or turning the rotary encoder should stop it; refer to the original timer.ino
- increase the BLY POMODORO font size by 25%
- there's a text at the 5'oclock position starting with a "3"..., remove this
