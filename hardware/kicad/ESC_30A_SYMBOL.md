# ESC 30A BLDC

Symbol: `ESC_30A:ESC_30A_BLDC`. Reference prefix: `U`.
External wired module; no PCB footprint assigned.

| Pin | Name | Electrical type |
| --- | --- | --- |
| 1 | BAT+ | power_in |
| 2 | BAT- | power_in |
| 3 | U | passive |
| 4 | V | passive |
| 5 | W | passive |
| 6 | PWM | input |
| 7 | BEC/VCC | passive |
| 8 | GND | power_in |

Numbers are a local logical convention, not physical connector cavity numbers.
The photo shows a 30A marking, two power leads, three motor leads and a three-wire
control lead. Exact model, supply range, control protocol and BEC function/voltage
are not established from the image. Pin 7 is intentionally passive until verified
against the actual ESC. Motor phase U/V/W names are a local convention.

Add from the project's symbol chooser with `ESC_30A_BLDC`.
Preview: `ESC_30A.png`.
