# Check NDDK Version
PWD=$(pwd)

# Retrieve NDDK directory name
NDDK_DIR=$(echo ${PWD} | sed 's/\//\ /g' | awk '{print $3}')
SEPARATOR=-

get_len_upto_separator () {
  local idx=0
  local keyword

  keyword=${NDDK_DIR:$idx:1}
  while [ "$SEPARATOR" != "$keyword" ]
  do
    let "idx += 1"
    keyword=${NDDK_DIR:$idx:1}
  done

  let "idx += 1"
  return $idx
}

get_ndk_ver () {
  local nddk_ver
  local start=0
  local end=0

  get_len_upto_separator
  start=$?
  let "start += 1"
  end=`expr $start + 2`

  nddk_ver=$(echo $NDDK_DIR | cut -c $start-$end)
  echo ${nddk_ver} | sed -e 's/^[ \t]*//' -e 's/[ \t]*$//'
}

get_ndk_ver
