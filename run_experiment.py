
import csv
import time
import serial

PRIMARY_PORT = "COM3"
RESET_PORT = "COM8"
BAUD = 9600
TRIALS = 100
OUTPUT_FILE = "fdir_results_step2.csv"

# First, deliberately test that Uno #2 can reset Uno #1.
TEST_RESET_ON_START = True


def read_response(ser):
    return ser.readline().decode("utf-8", errors="replace").strip()


def send_command(ser, text):
    ser.write((text + ";").encode("utf-8"))
    ser.flush()
    return read_response(ser)


def parse_line(line, prefix):
    if not line or not line.startswith(prefix + ","):
        return None
    return line.split(",")


def ping_primary(primary):
    # Ignore any old startup messages before checking responsiveness.
    primary.reset_input_buffer()
    return send_command(primary, "P") == "PONG"


def reset_primary(primary, reset_controller):
    """Ask Uno #2 to reset Uno #1, then verify Uno #1 responds."""
    print("Requesting primary reset through Uno #2...")

    reset_controller.reset_input_buffer()
    response = send_command(reset_controller, "R")
    print(f"Reset controller response: {response!r}")

    if response != "RESETTING_PRIMARY":
        print("WARNING: Unexpected reset-controller response.")
        return False

    # Allow the primary to reboot and print its startup message.
    time.sleep(2)
    primary.reset_input_buffer()

    # Try several times in case startup takes a little longer.
    for attempt in range(3):
        if send_command(primary, "P") == "PONG":
            print("Primary recovered and responds to ping.")
            return True
        time.sleep(1)
        primary.reset_input_buffer()

    print("ERROR: Primary did not respond after reset.")
    return False


def main():
    rows = []

    # Opening these ports may reset both Uno boards.
    with serial.Serial(PRIMARY_PORT, BAUD, timeout=2) as primary, \
         serial.Serial(RESET_PORT, BAUD, timeout=2) as reset_controller:

        time.sleep(2)
        primary.reset_input_buffer()
        reset_controller.reset_input_buffer()

        print(f"Primary: {PRIMARY_PORT}")
        print(f"Reset controller: {RESET_PORT}")

        if send_command(reset_controller, "P") != "PONG":
            raise RuntimeError(
                "Reset controller did not respond. Check COM8 and its sketch."
            )

        if not ping_primary(primary):
            if not reset_primary(primary, reset_controller):
                raise RuntimeError(
                    "Primary is not responding. Check COM3 and its sketch."
                )

        # Explicitly verify the hardware reset mechanism once.
        if TEST_RESET_ON_START:
            if not reset_primary(primary, reset_controller):
                raise RuntimeError(
                    "Startup reset test failed. Check wiring and sketches."
                )

        info = parse_line(send_command(primary, "INFO"), "INFO")
        if info is None:
            raise RuntimeError(
                "Could not read INFO from primary after reset."
            )

        buffer_size = int(info[1])
        base_address = info[2]

        print(f"Buffer size: {buffer_size} bytes")
        print(f"Buffer start address: {base_address}")
        print(f"Running {TRIALS} trials...\n")

        for trial in range(TRIALS):
            index = (trial * 7) % buffer_size
            bit = (trial * 3) % 8
            expected_value = 42 + index

            row = {
                "trial": trial + 1,
                "index": index,
                "bit": bit,
                "base_address": base_address,
                "detected": 0,
                "test_string_ok": 0,
                "responsive": 0,
                "recovered": 0,
                "reset_attempted": 0,
                "reset_success": 0,
                "classification": "",
            }

            # Select the byte.
            moved = parse_line(
                send_command(primary, f"M,{index}"), "MOVED"
            )

            if moved is None:
                row["reset_attempted"] = 1
                row["reset_success"] = int(
                    reset_primary(primary, reset_controller)
                )
                row["classification"] = (
                    "RESET_RECOVERED" if row["reset_success"]
                    else "RESET_FAILED"
                )
                rows.append(row)
                if not row["reset_success"]:
                    break
                continue

            # Inject one bit flip.
            flipped = parse_line(
                send_command(primary, f"F,{bit}"), "FLIPPED"
            )

            if flipped is None:
                row["reset_attempted"] = 1
                row["reset_success"] = int(
                    reset_primary(primary, reset_controller)
                )
                row["classification"] = (
                    "RESET_RECOVERED" if row["reset_success"]
                    else "RESET_FAILED"
                )
                rows.append(row)
                if not row["reset_success"]:
                    break
                continue

            row["detected"] = int(
                int(flipped[4]) != expected_value
            )

            # Check the test string.
            test = parse_line(send_command(primary, "T"), "TEST")

            if test is None:
                row["reset_attempted"] = 1
                row["reset_success"] = int(
                    reset_primary(primary, reset_controller)
                )
                row["classification"] = (
                    "RESET_RECOVERED" if row["reset_success"]
                    else "RESET_FAILED"
                )
                rows.append(row)
                if not row["reset_success"]:
                    break
                continue

            row["test_string_ok"] = int(test[-1] == "OK")

            # Check responsiveness.
            row["responsive"] = int(ping_primary(primary))

            if not row["responsive"]:
                row["reset_attempted"] = 1
                row["reset_success"] = int(
                    reset_primary(primary, reset_controller)
                )
                row["classification"] = (
                    "RESET_RECOVERED" if row["reset_success"]
                    else "RESET_FAILED"
                )
                rows.append(row)
                if not row["reset_success"]:
                    break
                continue

            # Restore the selected byte and verify it.
            restored_command = parse_line(
                send_command(primary, f"S,{expected_value}"), "SET"
            )
            restored = parse_line(
                send_command(primary, "O"), "POINTER"
            )

            if restored_command is not None and restored is not None:
                row["recovered"] = int(
                    int(restored[3]) == expected_value
                    and int(restored[4]) == expected_value
                    and restored[5] == "1"
                )

            if not row["test_string_ok"]:
                row["classification"] = "TEST_STRING_CORRUPTION"
            elif not row["detected"]:
                row["classification"] = "FAULT_NOT_DETECTED"
            elif row["recovered"]:
                row["classification"] = "DATA_FAULT_RECOVERED"
            else:
                row["classification"] = "DATA_FAULT_RECOVERY_FAILED"

            rows.append(row)

            print(
                f"Trial {trial + 1:03}/{TRIALS}: "
                f"index={index}, bit={bit}, "
                f"detected={row['detected']}, "
                f"recovered={row['recovered']}, "
                f"{row['classification']}"
            )

        # Save records, including partial runs.
        if rows:
            with open(
                OUTPUT_FILE, "w", newline="", encoding="utf-8"
            ) as file:
                writer = csv.DictWriter(
                    file, fieldnames=list(rows[0].keys())
                )
                writer.writeheader()
                writer.writerows(rows)

        print("\n--- STEP 2 SUMMARY ---")
        print(f"Trials recorded: {len(rows)}/{TRIALS}")
        print(
            "Reset attempts during trials: "
            f"{sum(r['reset_attempted'] for r in rows)}"
        )
        print(
            "Successful resets during trials: "
            f"{sum(r['reset_success'] for r in rows)}"
        )
        print(
            "Detected data faults: "
            f"{sum(r['detected'] for r in rows)}"
        )
        print(
            "Verified data recoveries: "
            f"{sum(r['recovered'] for r in rows)}"
        )
        print(f"CSV saved to: {OUTPUT_FILE}")


if __name__ == "__main__":
    main()
