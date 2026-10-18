name: Bug report
description: Report a firmware problem
title: "[BUG] "
labels: ["bug"]
body:
  - type: dropdown
    id: board
    attributes:
      label: Board
      options:
        - n16r8 (ESP32-S3)
        - waveshare_8di8do
        - s3_touch7
        - esp32p4
        - esp32p4cam
    validations:
      required: true
  - type: input
    id: pack
    attributes:
      label: Pack
      description: e.g. RAMBOX, WAVESHARE, P4_CNC
    validations:
      required: true
  - type: input
    id: version
    attributes:
      label: Firmware version
      description: From /api/status (e.g. v1.120)
      placeholder: v1.120
    validations:
      required: true
  - type: textarea
    id: what
    attributes:
      label: What happened?
      description: Steps to reproduce, if known
    validations:
      required: true
  - type: textarea
    id: expected
    attributes:
      label: What did you expect?
    validations:
      required: false
  - type: textarea
    id: logs
    attributes:
      label: Relevant logs
      description: Paste Web Serial Monitor output or /api/status excerpt. Strip WiFi passwords and personal emails.
      render: shell
    validations:
      required: false
  - type: checkboxes
    id: checked
    attributes:
      label: Checklist
      options:
        - label: Reproduces on the latest version
        - label: Searched existing issues (open and closed)
