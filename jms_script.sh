#!/usr/bin/env bash

print_usage() {
  echo "Usage: $0 -l <path> -c <command>"
  exit 1
}

path=
cmd=
cmd_arg=

i=1

# first parse user input
while [ $i -le "$#" ]
do
  case "${!i}" in
    "-l" )
      next=$((i + 1))

      if [ -n "$path" ]
      then
        echo "ERROR: path argument was provided twice" 2>&1
        print_usage
      fi

      if [ "$#" -lt "$next" ]
      then
        print_usage
      fi

      path=${!next}
      i=$((i + 1))
      ;;

    "-c" )
      next=$((i + 1))

      if [ -n "$cmd" ]
      then
        echo "ERROR: command argument was provided twice" 2>&1
        print_usage
      fi

      if [ "$#" -lt "$next" ]
      then
        print_usage
      fi

      cmd=${!next}

      if [ "$cmd" = "size" ] && [ $((next + 1)) -le "$#" ]
      then
        idx=$((next + 1))
        n=${!idx}

        # Check if it's a number
        if [[ "$n" =~ ^[0-9]+$ ]]
        then
          cmd_arg=$n
          i=$((i + 1))
        fi
      fi

      i=$((i + 1))
      ;;
    * )
      echo "Unknown flag ${!i}"
      print_usage
      ;;
  esac

  i=$((i + 1))
done

if [ -z "$path" ]
then
  echo "ERROR: Argument path (-l) has to be provided" 2>&1
  print_usage
fi

if [ -z "$cmd" ]
then
  echo "ERROR: Argument cmd (-c) has to be provided" 2>&1
  print_usage
fi

find_outputs() {
  find "$1" -maxdepth 1 -mindepth 1 -type d -name 'outputs_*' -print0
}

case "$cmd" in
  "list" )
    find_outputs "$path" | tr '\0' '\n'
    ;;
  "size" )
    result=$(find_outputs "$path" | xargs -r -0 du -sb | sort -n )
    # | cut -f 2 -s optionally add this to the above command to remove the size number
    # I kept them in because they are helpful

    if [ -n "$cmd_arg" ]
    then
      result=$(echo "$result" | tail -n"$cmd_arg")
    fi

    echo "$result"
    ;;
  "purge" )
    find_outputs "$path" | xargs -r -0 rm -r
    ;;
esac
