# Temat 10 - Magazyn firmy spedycyjnej

**Imię i Nazwisko:** Mateusz Karpiuk
**Nr Albumu:** 155185

**Link do GitHub:**https://github.com/carpyook/Projekt_SO_Temat_10

## Opis zadania

Projekt polega na napisaniu programów symulujących działanie magazynu firmy spedycyjnej, w którym pracują dyspozytor, pracownicy oraz ciężarówki.
Głównym elementem magazynu jest taśma transportowa, przy której pracują trzy osoby (P1, P2, P3) układające przesyłki, oraz pracownik (P4) odpowiedzialny za przesyłki ekspresowe. Towary trafiają do ciężarówek, które po załadunku rozwożą przesyłki i wracają do magazynu.
Symulacja uwzględnia ograniczenia fizyczne taśmy (pojemność, udźwig), parametry paczek (wymiary, waga) oraz logistykę pojazdów (ładowność, czas powrotu). Cały proces jest sterowany sygnałami wydawanymi przez dyspozytora.
## Opis działania


1.  **Praca pracowników P1, P2, P3**
    *   Układanie na taśmę przesyłek o gabarytach: A (64x38x8 cm), B (64x38x19 cm) i C (64x38x41 cm).
    *   Generowanie losowej wagi paczek z zakresu 0,1 kg – 25,0 kg (z założeniem: mniejsza paczka = mniejszy ciężar).
    *   Wszyscy pracownicy starają się układać przesyłki na taśmie najszybciej jak to możliwe.
2.  **Działanie taśmy transportowej**
    *   Transport maksymalnie K przesyłek w danej chwili.
    *   Kontrola udźwigu taśmy do maksymalnie M jednostek masy 
    *   Transport przesyłek do ciężarówki dokładnie w takiej kolejności, w jakiej zostały położone.

3.  **Obsługa przesyłek ekspresowych (Pracownik P4)**
    *   Dostarczanie pakietu przesyłek ekspresowych bezpośrednio do ciężarówki (z pominięciem taśmy) po otrzymaniu **sygnału 2** od dyspozytora.

4.  **Logistyka ciężarówek**
    *  Załadunek ciężarówek o ładowności W [kg] oraz objętości V [m3] zawsze do pełna ,chyba że dyspozytor zdecyduje inaczej.
    *   Obsługa floty składającej się z N ciężarówek.
    *   Symulacja powrotu ciężarówki do magazynu po czasie T od rozwiezienia przesyłek.
    *   Podstawianie nowej ciężarówki natychmiast po zapełnieniu poprzedniej, jeśli jest dostępna.

5.  **Sterowanie przez Dyspozytora (Sygnały)**
    *   **Sygnał 1:** Nakaz odjazdu ciężarówki z niepełnym ładunkiem.
    *   **Sygnał 2:** Polecenie dla pracownika P4 dotyczące załadunku przesyłek ekspresowych.
    *   **Sygnał 3:** Polecenie zakończenia pracy dla pracowników, ciężarówki kończą pracę dopiero po rozwiezieniu wszystkich przesyłek.

6.  **Raportowanie**
    *   Zapis raportu z przebiegu symulacji w pliku tekstowym.
