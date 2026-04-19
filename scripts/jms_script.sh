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
        cmd_arg=${!idx}

        i=$((i + 1))
      fi

      i=$((i + 1))
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
  find "$1" -name "outputs_*"
}

case "$cmd" in
  "list" )
    find_outputs "$path"
    ;;
  "size" )
    result=$(find_outputs "$path" | xargs du -sb | sort -n)

    if [ -n "$cmd_arg" ]
    then
      result=$(echo "$result" | tail -n"$cmd_arg")
    fi

    echo "$result"
    ;;
  "purge" )
    find_outputs "$path" | xargs rm -r
    ;;
esac
