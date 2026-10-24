'use strict';

const { createHash } = require('crypto');
const fs = require('fs');
const path = require('path');

const BOARD = 'esp32-s3';
const DEVICE = 'esp32-ac-01';
const projectRoot = path.resolve(__dirname, '..');
const releaseDirectory = path.join(projectRoot, 'firmware', 'releases', DEVICE);
const manifestPath = path.join(releaseDirectory, 'manifest.json');
const [version, binaryArgument, ...releaseNameParts] = process.argv.slice(2);
const releaseName = releaseNameParts.join(' ').trim() || `Firmware ${version || ''}`.trim();

function parseVersion(value) {
  const match = /^(\d+)\.(\d+)\.(\d+)$/.exec(value || '');
  if (!match) throw new Error('Version must use MAJOR.MINOR.PATCH format.');
  return match.slice(1).map((part) => Number(part));
}

function compareVersions(left, right) {
  const leftParts = parseVersion(left);
  const rightParts = parseVersion(right);
  for (let index = 0; index < 3; index += 1) {
    if (leftParts[index] > rightParts[index]) return 1;
    if (leftParts[index] < rightParts[index]) return -1;
  }
  return 0;
}

function main() {
  if (!version || !binaryArgument) {
    throw new Error('Usage: npm run firmware:release -- <version> <compiled-firmware.bin> [release-name]');
  }
  parseVersion(version);

  const sourcePath = path.resolve(binaryArgument);
  if (!fs.existsSync(sourcePath) || !fs.statSync(sourcePath).isFile()) {
    throw new Error(`Firmware binary not found: ${sourcePath}`);
  }
  if (path.extname(sourcePath).toLowerCase() !== '.bin') {
    throw new Error('Input must be the compiled .bin firmware image.');
  }

  const sketchPath = path.join(projectRoot, 'firmware', 'esp32_drawbook', 'esp32_drawbook.ino');
  const sketch = fs.readFileSync(sketchPath, 'utf8');
  const versionMatch = /constexpr char FIRMWARE_VERSION\[\] = "([^\"]+)";/.exec(sketch);
  if (!versionMatch || versionMatch[1] !== version) {
    throw new Error(`Set FIRMWARE_VERSION in ${sketchPath} to ${version}, then compile the sketch first.`);
  }

  const currentManifest = JSON.parse(fs.readFileSync(manifestPath, 'utf8'));
  if (compareVersions(version, currentManifest.version) <= 0) {
    throw new Error(`Release version ${version} must be newer than ${currentManifest.version}.`);
  }

  const binaryName = `drawbook-v${version}.bin`;
  const destinationPath = path.join(releaseDirectory, binaryName);
  const binary = fs.readFileSync(sourcePath);
  if (binary.length === 0) throw new Error('Firmware binary is empty.');

  fs.mkdirSync(releaseDirectory, { recursive: true });
  fs.copyFileSync(sourcePath, destinationPath);

  const manifest = {
    board: BOARD,
    enabled: true,
    name: releaseName,
    version,
    firmware_path: `/firmware/${DEVICE}/${binaryName}`,
    sha256: createHash('sha256').update(binary).digest('hex')
  };
  const temporaryManifestPath = `${manifestPath}.tmp`;
  fs.writeFileSync(temporaryManifestPath, `${JSON.stringify(manifest, null, 2)}\n`);
  fs.renameSync(temporaryManifestPath, manifestPath);

  console.log(`Published ${binaryName} (${binary.length} bytes, SHA-256 ${manifest.sha256}).`);
  console.log('Commit the firmware source version, manifest, and release binary, then deploy the service.');
}

try {
  main();
} catch (error) {
  console.error(`[firmware:release] ${error.message}`);
  process.exitCode = 1;
}
