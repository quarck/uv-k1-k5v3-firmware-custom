cmake --fresh --preset Qrck
cmake --build --preset Qrck -j

cmake --fresh --preset QrckNoTx
cmake --build --preset QrckNoTx -j

cmake --fresh --preset Max
cmake --build --preset Max -j

mkdir -p out
cp build/Qrck/*.bin out
cp build/QrckNoTx/*.bin out
cp build/Max/*.bin out

echo "Build lands in build/Qrck/ (and QrckNoTx)"
