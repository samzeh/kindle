#!/bin/sh
# Downloads four public-domain books from Project Gutenberg as EPUB files
# into simulator/books/, for trying the reader in the simulator.
#
#   sh tools/fetch_books.sh
set -e
dir="$(cd "$(dirname "$0")/.." && pwd)/simulator/books"
mkdir -p "$dir"
for book in "1342 pride_and_prejudice" "84 frankenstein" "345 dracula" "2701 moby_dick"; do
  set -- $book
  if [ -f "$dir/$2.epub" ]; then
    echo "have $2.epub"
  else
    echo "downloading $2.epub"
    curl -sfL -o "$dir/$2.epub" "https://www.gutenberg.org/ebooks/$1.epub3.images"
  fi
done
echo "books are in $dir"
