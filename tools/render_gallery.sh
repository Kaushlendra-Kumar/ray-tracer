#!/usr/bin/env bash
# Renders every demo scene at its own settings into docs/images/, plus a
# progressive-convergence series for the glass/metal Cornell box.
# Usage: tools/render_gallery.sh [build-dir]
set -euo pipefail
cd "$(dirname "$0")/.."
BUILD=${1:-build}
mkdir -p docs/images
for scene in first_light materials mirrors soft_shadows emissive_glow cornell_box mesh_bvh sphere_field cornell_glass_metal; do
  echo "== $scene"
  "$BUILD/rt" "assets/scenes/$scene.json" -o "docs/images/$scene.png" 2>&1 | tail -1 | tee "docs/images/$scene.log"
done
echo "== convergence series"
mkdir -p docs/images/convergence && rm -f docs/images/convergence/*.png
"$BUILD/rt" assets/scenes/cornell_glass_metal.json -o /tmp/rt_convergence_final.png \
  --width 256 --height 256 --spp 1024 --max-pass 1024 --snapshot-dir docs/images/convergence 2>&1 | tail -1
if python3 -c "import PIL" 2>/dev/null; then
  python3 tools/montage.py && rm -rf docs/images/convergence
else
  echo "Pillow not installed: skipping docs/images/convergence.png (snapshots left in docs/images/convergence)"
fi
