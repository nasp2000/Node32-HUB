name: Feature request
description: Suggest an idea (pack, page, protocol, hardware support)
title: "[FEATURE] "
labels: ["enhancement"]
body:
  - type: textarea
    id: problem
    attributes:
      label: Problem
      description: What limitation or use case motivates this?
    validations:
      required: true
  - type: textarea
    id: proposal
    attributes:
      label: Proposed solution
    validations:
      required: true
  - type: textarea
    id: alternatives
    attributes:
      label: Alternatives considered
    validations:
      required: false
  - type: dropdown
    id: area
    attributes:
      label: Area
      multiple: true
      options:
        - Firmware / pack
        - Web UI page
        - REST API
        - Build system / CI
        - Docs
    validations:
      required: false
