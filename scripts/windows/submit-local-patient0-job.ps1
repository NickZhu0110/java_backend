$ErrorActionPreference = "Stop"

$json = '{"modelName":"SEGMENT-CACS","inputPath":"C:/cac-runtime/data/cac/cases/patient0/dicom","outputPath":"C:/cac-runtime/data/cac/jobs/job_patient0_cpu","fileType":"dcm","device":"cpu","useZeroModule":false}'

curl.exe -X POST http://localhost:6006/api/jobs `
  -H "Content-Type: application/json" `
  -d $json

Write-Host ""
Write-Host "Query job list:"
Write-Host "curl.exe http://localhost:6006/api/jobs"
Write-Host ""
Write-Host "Query latest result after the worker finishes:"
Write-Host "curl.exe http://localhost:6006/api/jobs/1/result"
