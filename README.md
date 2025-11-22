# ConsoleDeck

# Console Deck - User Guide

Welcome to Console Deck! This application allows you to use your Arduino Nano as a customizable macro/control deck for your computer.

## Installation

1. Run the installer `ConsoleDeck_Installer.exe`
2. Follow the installation wizard
3. Choose installation location (default is recommended)
4. Optional: Create desktop shortcut and/or auto-start with Windows

## Arduino Setup

Before using the application, you need to flash your Arduino Nano:

1. Open Arduino IDE (download from https://www.arduino.cc if needed)
2. Connect your Arduino Nano via USB
3. Find the sketch file in the installation folder:
   - Default location: `C:\Program Files\Console Deck\Arduino\ArduinoNANODESKTOP.ino`
4. Open the sketch in Arduino IDE
5. Select your board: **Tools → Board → Arduino Nano**
6. Select your processor: **Tools → Processor → ATmega328P (Old Bootloader)** or **ATmega328P**
7. Select your port: **Tools → Port → COM# (Arduino Nano)**
8. Click the **Upload** button (arrow icon)
9. Wait for "Done uploading" message
10. **Note which COM port your Arduino is using** (you'll need this for the app)

## First Launch

1. Launch Console Deck from:
   - Start Menu → Console Deck
   - Desktop shortcut (if created during installation)
   - Or run `ConsoleDeck.exe` directly

2. The application will open showing 9 customizable buttons

3. If Arduino connection fails, check the COM port:
   - Open Windows Device Manager
   - Expand "Ports (COM & LPT)"
   - Find your Arduino (e.g., "USB Serial Device (COM6)")
   - Note the COM number

## Configuring Buttons

### To Configure a Button:

1. Click on any numbered button (1-9)
2. A configuration dialog will appear
3. Select the action type:
   - **None**: No action (empty button)
   - **Link**: Open a website
   - **Executable**: Launch a program or shortcut
   - **Keyboard Shortcut**: Send key combinations
   - **Text Input**: Type predefined text

4. Enter the required information based on action type:

   **For Link:**
   - Enter full URL including `https://`
   - Example: `https://www.youtube.com`

   **For Executable:**
   - Click "Browse" to select file
   - Can be `.exe`, `.bat`, `.cmd`, or `.lnk` (shortcut) files
   - Example: `C:\Program Files\Google\Chrome\chrome.exe`

   **For Keyboard Shortcut:**
   - Enter key combination using `+` separator
   - Use lowercase for letter keys
   - Examples:
     - `ctrl+c` - Copy
     - `ctrl+shift+a` - Custom shortcut
     - `win+d` - Show desktop
     - `alt+tab` - Switch windows

   **For Text Input:**
   - Type any text you want to auto-type
   - Example: `Hello, this is automated text!`

5. Click **"Test Action"** to verify it works correctly
6. Click **"Save"** to confirm configuration
7. Click **"Cancel"** to discard changes

### Button Colors:

- **Gray**: No action configured
- **Orange gradient**: Action configured and ready to use
- **Special colors** for keyboard/text actions

## Using the Arduino Deck

Once configured, simply press the physical buttons on your Arduino deck to trigger the corresponding actions!

### Special Arduino Controls:

If your Arduino has additional components:

- **Volume Encoder**: Rotate to adjust system volume up/down
- **Mute Button**: Press to toggle system mute (Arduino sends `MUTE`)
- **Media Button**: Press to play/pause media (Arduino sends `MEDIA`)

## Keyboard Shortcuts (Bonus Feature)

You can also trigger button actions from your computer keyboard:

- **Ctrl+Alt+1** through **Ctrl+Alt+9** - Execute button actions 1-9

This is useful for testing or as a backup when Arduino isn't connected.

## Configuration File

Your button configurations are automatically saved to `config.json` in the application directory. This file is:
- Created automatically on first use
- Updated whenever you save button configurations
- Can be backed up for safekeeping
- Located in `C:\Program Files\Console Deck\config.json` by default

## System Tray

The application minimizes to the system tray (notification area near clock) when you close the window:

- **Double-click tray icon**: Show/hide the main window
- **Right-click tray icon**: Open menu
  - **Show**: Display main window
  - **Hide**: Minimize to tray
  - **Quit**: Exit application completely

This allows Console Deck to run in the background without taking up taskbar space.

## Troubleshooting

### Arduino Not Connecting

**Problem:** "Could not connect to serial port" error

**Solutions:**
1. Verify Arduino is plugged into USB port
2. Check if Arduino sketch is uploaded correctly
3. Check COM port number:
   - Open Device Manager (Win+X → Device Manager)
   - Look under "Ports (COM & LPT)"
   - Find Arduino's COM port number (e.g., COM6)
4. Try different USB port or cable
5. Restart the application

### Changing COM Port

If your Arduino uses a different COM port than the default (COM6):

1. In the application, you may need to restart with command line:
   - Open Command Prompt
   - Navigate to installation folder: `cd "C:\Program Files\Console Deck"`
   - Run: `ConsoleDeck.exe --port COM3` (replace COM3 with your port)

### Buttons Not Responding

**Problem:** Physical buttons don't trigger actions

**Solutions:**
1. Check Arduino is connected (green light on Arduino should be on)
2. Verify serial connection in application
3. Re-upload Arduino sketch
4. Check button is configured (should be orange, not gray)
5. Check log file: `desktop_deck.log` in installation directory

### Actions Not Executing

**Problem:** Button configured but action doesn't work

**Solutions:**

**For Links:**
- Ensure URL includes `https://` or `http://`
- Test URL in web browser first

**For Executables:**
- Verify file path is correct
- Check file still exists at that location
- Run as administrator if needed
- For shortcuts (.lnk), ensure target still exists

**For Keyboard Shortcuts:**
- Use lowercase letters: `ctrl+c` not `Ctrl+C`
- Use `+` to separate keys: `ctrl+shift+a`
- Test the shortcut manually to ensure it works in target app

**For Text:**
- Ensure you've clicked into text field where you want text typed
- Some applications may block automated typing

### Application Won't Start

**Solutions:**
1. Check if already running in system tray
2. Run as administrator (right-click → Run as administrator)
3. Check Windows Event Viewer for error details
4. Reinstall application

### Log Files

For detailed troubleshooting, check the log file:
- Location: `C:\Program Files\Console Deck\desktop_deck.log`
- Contains connection status, errors, and activity
- Helps diagnose problems

## Advanced Usage

### Command Line Options

You can run Console Deck with special options:

```
ConsoleDeck.exe --nogui              # Run without GUI (console mode)
ConsoleDeck.exe --port COM3          # Use specific COM port
ConsoleDeck.exe --baudrate 115200    # Use different baud rate
ConsoleDeck.exe --list-ports         # Show available ports and exit
```

### Multiple Instances

Only one instance of Console Deck should run at a time. The application will prevent multiple instances.

## Uninstalling

To remove Console Deck:

1. Close the application (right-click tray icon → Quit)
2. Open Windows Settings
3. Go to **Apps → Apps & Features**
4. Find **"Console Deck"** in the list
5. Click and select **Uninstall**
6. Follow the uninstall wizard

## Tips and Best Practices

1. **Test Before Using**: Always use "Test Action" before saving
2. **Backup Config**: Copy `config.json` to save your button configurations
3. **Label Buttons**: Keep track of which physical button is which number
4. **Start Simple**: Configure one button at a time and test
5. **Use Shortcuts**: Shortcuts (.lnk files) are more reliable than direct .exe paths

## Common Use Cases

### For Streamers:
- Button 1: Start/stop recording
- Button 2: Mute microphone
- Button 3: Switch scenes
- Button 4: Open chat
- Button 5-9: Custom commands

### For Productivity:
- Button 1: Open email
- Button 2: Open calendar
- Button 3: Open project folder
- Button 4: Launch IDE
- Button 5: Insert email signature (text)

### For Gaming:
- Buttons 1-9: Macro commands (keyboard shortcuts)
- Quick access to game launchers
- Voice chat mute/unmute

## Getting Help

If you encounter issues:

1. Check this README first
2. Review the log file: `desktop_deck.log`
3. Verify Arduino sketch is uploaded correctly
4. Test with keyboard shortcuts (Ctrl+Alt+1-9) to isolate Arduino vs software issues

---

**Enjoy your Console Deck! Happy button pressing! 🎮**
