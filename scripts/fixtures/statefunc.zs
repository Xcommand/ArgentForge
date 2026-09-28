class Foo : Actor
{
  state void A_Idle()
  {
    A_Look();
  }
  States
  {
    SPWN A 1
  }
}
