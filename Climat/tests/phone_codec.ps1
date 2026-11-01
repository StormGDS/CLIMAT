# Independent reference codec for the phone protocol v3 (no MCU/Flash access).
function Get-PhoneCrc([byte[]]$Bytes) {
  [uint32]$crc=65535
  foreach($value in $Bytes) {
    $crc=$crc -bxor ([uint32]$value -shl 8)
    for($bit=0;$bit -lt 8;$bit++) {
      if($crc -band 32768){$crc=(($crc -shl 1) -bxor 4129) -band 65535}
      else {$crc=($crc -shl 1) -band 65535}
    }
  }
  return [uint16]$crc
}
function ConvertTo-PhoneCobs([byte[]]$Raw) {
  $result=[Collections.Generic.List[byte]]::new();$result.Add(0);$result.Add(0)
  $codeAt=1;[byte]$code=1
  foreach($value in $Raw) {
    if($value -eq 0){$result[$codeAt]=$code;$codeAt=$result.Count;$result.Add(0);$code=1}
    else {$result.Add($value);$code++}
  }
  $result[$codeAt]=$code;$result.Add(0);return ,$result.ToArray()
}
function New-PhoneFrame([byte]$Op,[uint16]$Sequence,[byte[]]$Payload=@(),[byte]$Version=3) {
  if($Payload.Length -gt 64){throw 'Payload exceeds 64 bytes'}
  [byte[]]$raw=@($Version,$Op,($Sequence -band 255),($Sequence -shr 8),$Payload.Length)+$Payload
  $crc=Get-PhoneCrc $raw
  $raw+=[byte]($crc -band 255);$raw+=[byte]($crc -shr 8)
  return ,(ConvertTo-PhoneCobs $raw)
}
function Read-PhoneFrame([byte[]]$Wire) {
  if($Wire.Length -lt 3 -or $Wire[0] -ne 0 -or $Wire[-1] -ne 0){throw 'Frame delimiters'}
  $raw=[Collections.Generic.List[byte]]::new();$at=1
  while($at -lt $Wire.Length-1){
    $code=[int]$Wire[$at++];if(!$code){throw 'COBS zero'}
    for($i=1;$i -lt $code;$i++){
      if($at -ge $Wire.Length-1 -or !$Wire[$at]){throw 'COBS length'}
      $raw.Add($Wire[$at++])
    }
    if($code -ne 255 -and $at -lt $Wire.Length-1){$raw.Add(0)}
  }
  if($raw.Count -lt 7 -or $raw.Count -ne $raw[4]+7){throw 'Frame length'}
  [byte[]]$bytes=$raw.ToArray()
  $crc=Get-PhoneCrc $bytes[0..($bytes.Length-3)]
  if($crc -ne ($bytes[-2]+256*$bytes[-1])){throw 'Frame CRC'}
  [byte[]]$payload=if($bytes[4]){$bytes[5..(4+$bytes[4])]}else{@()}
  return [pscustomobject]@{Version=$bytes[0];Op=$bytes[1];Sequence=($bytes[2]+256*$bytes[3]);Payload=$payload;Raw=$bytes}
}
