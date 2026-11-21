# Central update architecture for SoftzenAgency

## Parts

1. Master reference database kept by SoftzenAgency (`master.db`), maintained with
   `refpack import` from official files.
2. Static HTTPS file host (any web server or object storage with a CDN).
3. Each pharmacy installation, which downloads on its own when internet is available.

Nothing is uploaded by the pharmacy. Requests carry only the app version in the User-Agent.
Sales, patients, customers, prices and stock never leave the PC.

## Files on the server

```
/medicarepro/stable/manifest.json       list of available updates
/medicarepro/stable/manifest.json.sig   Ed25519 signature of manifest.json (base64)
/medicarepro/stable/ref/delta-4-5.pkg   only the records changed between versions 4 and 5
/medicarepro/stable/ref/full-5.pkg      every record (for new or very old installs)
/medicarepro/stable/app/MediCarePro-Setup-1.1.0.exe
```

## Publishing a medicine data update

```
refpack import  --db master.db --csv drap_2026_12.csv --source-code drap-2026-12 \
                --source-name "DRAP registered products list, December 2026" --verified
refpack export  --db master.db --base 4 --out delta-4-5.pkg --description "December 2026 list"
refpack export  --db master.db --full --out full-5.pkg
refpack sign    --key update_private.pem --type reference_delta --version 5 --base-version 4 \
                --file delta-4-5.pkg --url https://updates.example.com/medicarepro/stable/ref/delta-4-5.pkg \
                --title "Medicine list December 2026" --components medicine_catalog,manufacturers
```

Paste each `sign` output into the `items` array of `manifest.json`, raise `sequence` by one, then:

```
refpack manifest --key update_private.pem --manifest manifest.json
```

Upload the packages first, then `manifest.json` and `manifest.json.sig`.

## Manifest format

```json
{
  "format": "medicarepro.manifest/1",
  "publisher": "SoftzenAgency",
  "sequence": 12,
  "generated_at": "2026-12-01T00:00:00Z",
  "expires_at": "2027-03-01T00:00:00Z",
  "items": [ { "id": "...", "type": "reference_delta | reference_full | software | translations | receipt_templates",
               "version": "5", "base_version": "4", "title": "...", "description": "...",
               "published_at": "2026-12-01", "components": ["medicine_catalog"], "notes": ["..."],
               "major": false, "url": "https://...", "sha256": "...", "size": 12345, "signature": "base64" } ]
}
```

## Checks done by the app

1. Manifest signature with the public key built into the app.
2. Manifest sequence never goes backwards (replay protection) and expiry date.
3. Every URL is HTTPS and on the allowed hosts list. Redirects must stay on allowed hosts.
4. Download size is capped at the announced size. SHA-256 must match.
5. Package signature covers `type:version:sha256`, so a valid file cannot be reused as another update.
6. Package content is validated record by record. Duplicates and unknown sources are rejected.
7. Safety backup, one transaction, integrity check, automatic restore on failure.
8. Every step is logged in `update_*` tables in the pharmacy database.

## Software updates

The admin sees installed and available versions, notes and fixes. Download is verified the same
way. Install asks for confirmation (with an extra notice for major updates), backs up both
databases, records a pending update marker, starts the Inno Setup installer and closes the app.
The installer restarts the app. At start the app compares its version with the marker and records
success, or shows a recovery message with the backup folder. Nothing installs silently. The
Windows installer should also be Authenticode signed with the SoftzenAgency code signing
certificate.
