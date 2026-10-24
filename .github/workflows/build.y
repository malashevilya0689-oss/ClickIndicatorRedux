name: Build Geode Mod

on:
  push:
    branches: [ "main", "master" ]
  workflow_dispatch:

jobs:
  build:
    runs-on: ubuntu-latest
    steps:
      - name: Checkout Source
        uses: actions/checkout@v4

      - name: Build Mod for Windows & Android (GD 2.2081)
        uses: geode-sdk/build-geode-mod@main
        with:
          combine: true
          sdk: nightly

      - name: Upload .geode Package
        uses: actions/upload-artifact@v4
        with:
          name: ClickIndicatorRedux-Geode
          path: ${{ steps.build.outputs.build-output }}
